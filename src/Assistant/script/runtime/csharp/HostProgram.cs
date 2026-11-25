using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using RazorEnhanced;

namespace CrossUo.ScriptHost
{
    internal static class CrossUoScriptHostProgram
    {
        private static readonly HashSet<string> BuiltinRazorEnhancedTypes = new HashSet<string>(StringComparer.Ordinal)
        {
            "RazorEnhanced.ScriptInterruptedException",
            "RazorEnhanced.RpcClient",
            "RazorEnhanced.Point3D",
            "RazorEnhanced.Property",
            "RazorEnhanced.Item",
            "RazorEnhanced.Mobile",
            "RazorEnhanced.Player",
            "RazorEnhanced.Items",
            "RazorEnhanced.Mobiles",
            "RazorEnhanced.Target",
            "RazorEnhanced.Journal",
            "RazorEnhanced.Gumps",
            "RazorEnhanced.Timer",
            "RazorEnhanced.Misc",
            "RazorEnhanced.AutoLoot",
            "RazorEnhanced.BandageHeal",
            "RazorEnhanced.Organizer",
            "RazorEnhanced.Restock",
            "RazorEnhanced.Scavenger",
            "RazorEnhanced.PathFinding",
            "RazorEnhanced.Spells"
        };

        private static int Main(string[] args)
        {
            // Capture raw standard streams exclusively for JSON-RPC framing BEFORE any user code runs.
            Stream rawIn = Console.OpenStandardInput();
            Stream rawOut = Console.OpenStandardOutput();
            var utf8NoBom = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
            var rpcReader = new StreamReader(rawIn, utf8NoBom, detectEncodingFromByteOrderMarks: false);
            var rpcWriter = new StreamWriter(rawOut, utf8NoBom)
            {
                AutoFlush = true,
                NewLine = "\n"
            };

            // Redirect Console.Out to Console.Error so Console.WriteLine in user scripts never corrupts JSON-RPC.
            Console.SetOut(Console.Error);
            RpcClient.Initialize(rpcReader, rpcWriter);

            PosixSignalRegistration sigTermReg = null;
            PosixSignalRegistration sigIntReg = null;
            try
            {
                if (!OperatingSystem.IsWindows())
                {
                    sigTermReg = PosixSignalRegistration.Create(PosixSignal.SIGTERM, ctx =>
                    {
                        RpcClient.MarkInterrupted();
                        ctx.Cancel = false;
                        Environment.Exit(0);
                    });
                    sigIntReg = PosixSignalRegistration.Create(PosixSignal.SIGINT, ctx =>
                    {
                        RpcClient.MarkInterrupted();
                        ctx.Cancel = false;
                        Environment.Exit(0);
                    });
                }
            }
            catch
            {
                // Platform may not support POSIX signal registration; Console.CancelKeyPress handles Ctrl-C.
            }

            Console.CancelKeyPress += (sender, e) =>
            {
                RpcClient.MarkInterrupted();
                e.Cancel = false;
                Environment.Exit(0);
            };

            try
            {
                string[] scriptArgs = ParseScriptArgs(args);
                Misc.ScriptArgsInternal = scriptArgs;

                (Type scriptType, MethodInfo runMethod) = FindEntryPoint(Assembly.GetExecutingAssembly());
                if (scriptType == null || runMethod == null)
                {
                    Console.Error.WriteLine(
                        "CrossUO C# script host: no entry point found. Define a class with a Run() method " +
                        "(for example: namespace RazorEnhanced { public class Script { public void Run() { ... } } }).");
                    return 1;
                }

                object instance = null;
                if (!runMethod.IsStatic)
                {
                    instance = Activator.CreateInstance(scriptType, nonPublic: true);
                }

                ParameterInfo[] parameters = runMethod.GetParameters();
                object[] invokeArgs = parameters.Length == 1 && parameters[0].ParameterType == typeof(string[])
                    ? new object[] { scriptArgs }
                    : Array.Empty<object>();

                runMethod.Invoke(instance, invokeArgs);
                return 0;
            }
            catch (TargetInvocationException ex) when (ex.InnerException is ScriptInterruptedException)
            {
                return 0;
            }
            catch (ScriptInterruptedException)
            {
                return 0;
            }
            catch (TargetInvocationException ex)
            {
                Exception inner = ex.InnerException ?? ex;
                Console.Error.WriteLine(inner.ToString());
                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(ex.ToString());
                return 1;
            }
            finally
            {
                sigTermReg?.Dispose();
                sigIntReg?.Dispose();
                try { rpcWriter.Flush(); } catch { }
            }
        }

        private static string[] ParseScriptArgs(string[] cliArgs)
        {
            string envJson = Environment.GetEnvironmentVariable("XUO_SCRIPT_ARGS_JSON");
            if (!string.IsNullOrWhiteSpace(envJson))
            {
                try
                {
                    using (JsonDocument doc = JsonDocument.Parse(envJson))
                    {
                        if (doc.RootElement.ValueKind == JsonValueKind.Array)
                        {
                            var list = new List<string>();
                            foreach (JsonElement el in doc.RootElement.EnumerateArray())
                            {
                                list.Add(el.ValueKind == JsonValueKind.String ? (el.GetString() ?? "") : el.ToString());
                            }
                            return list.ToArray();
                        }
                    }
                }
                catch
                {
                    // Fall back to CLI args if env JSON is malformed.
                }
            }
            return cliArgs ?? Array.Empty<string>();
        }

        private static (Type, MethodInfo) FindEntryPoint(Assembly assembly)
        {
            const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic |
                                       BindingFlags.Instance | BindingFlags.Static |
                                       BindingFlags.DeclaredOnly;

            var candidates = new List<(int Priority, Type Type, MethodInfo Method)>();

            foreach (Type type in assembly.GetTypes())
            {
                if (!type.IsClass || type.IsAbstract || type.IsGenericTypeDefinition)
                {
                    continue;
                }
                if (type.Namespace == "CrossUo.ScriptHost")
                {
                    continue;
                }
                if (type.FullName != null && BuiltinRazorEnhancedTypes.Contains(type.FullName))
                {
                    continue;
                }
                if (type.DeclaringType != null &&
                    type.DeclaringType.FullName != null &&
                    BuiltinRazorEnhancedTypes.Contains(type.DeclaringType.FullName))
                {
                    continue;
                }

                MethodInfo runMethod = FindRunMethod(type, flags);
                if (runMethod == null)
                {
                    continue;
                }
                if (!runMethod.IsStatic &&
                    type.GetConstructor(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
                        null, Type.EmptyTypes, null) == null)
                {
                    continue;
                }

                int priority = 100;
                if (type.FullName == "RazorEnhanced.Script")
                {
                    priority = 0;
                }
                else if (type.Name == "Script")
                {
                    priority = 10;
                }
                else if (type.IsPublic)
                {
                    priority = 20;
                }
                else
                {
                    priority = 30;
                }

                if (!runMethod.IsPublic)
                {
                    priority += 5;
                }

                candidates.Add((priority, type, runMethod));
            }

            if (candidates.Count == 0)
            {
                return (null, null);
            }

            var best = candidates.OrderBy(c => c.Priority).ThenBy(c => c.Type.FullName, StringComparer.Ordinal).First();
            return (best.Type, best.Method);
        }

        private static MethodInfo FindRunMethod(Type type, BindingFlags flags)
        {
            MethodInfo[] methods = type.GetMethods(flags)
                .Where(m => string.Equals(m.Name, "Run", StringComparison.Ordinal) && !m.IsGenericMethodDefinition)
                .ToArray();

            MethodInfo zeroArg = methods.FirstOrDefault(m => m.GetParameters().Length == 0 && m.IsPublic)
                ?? methods.FirstOrDefault(m => m.GetParameters().Length == 0);
            if (zeroArg != null)
            {
                return zeroArg;
            }

            return methods.FirstOrDefault(m =>
            {
                ParameterInfo[] p = m.GetParameters();
                return p.Length == 1 && p[0].ParameterType == typeof(string[]);
            });
        }
    }
}
