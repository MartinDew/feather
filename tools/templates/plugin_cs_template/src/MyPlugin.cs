// TODO: rename this file, the class, the entry point, and the names in
// my_plugin.fext.

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

internal static class MyPlugin {
	// The generated [DllImport]s name "feather_c" with no path. The engine has
	// already loaded that library into the global symbol scope before it loads
	// any extension, so the main program's handle is the right answer: it finds
	// the copy the process already has instead of mapping a second one.
	//
	// Resolving explicitly also turns a lookup failure into a readable message
	// rather than an abort -- an exception cannot escape an
	// UnmanagedCallersOnly entry point.
	[ModuleInitializer]
	internal static void RegisterResolver() {
		NativeLibrary.SetDllImportResolver(typeof(MyPlugin).Assembly, static (name, assembly, searchPath) => {
			if (name != "feather_c") {
				return IntPtr.Zero;
			}
			IntPtr main = NativeLibrary.GetMainProgramHandle();
			if (NativeLibrary.TryGetExport(main, "feather_to_string", out _)) {
				return main;
			}
			if (NativeLibrary.TryLoad("libfeather_c.so", out IntPtr handle)) {
				return handle;
			}
			Console.Error.WriteLine("[my_plugin] could not resolve libfeather_c");
			return IntPtr.Zero;
		});
	}

	// Mirrors feather::InitLevel, which crosses the boundary as a uint8_t.
	private const byte InitLevelCore = 0;

	// The entry point named by my_plugin.fext. Called once per initialization
	// level the engine enters, ascending.
	[UnmanagedCallersOnly(EntryPoint = "register_my_plugin")]
	public static void Register(byte level) {
		try {
			if (level != InitLevelCore) {
				return;
			}
			Console.WriteLine("[my_plugin] hello from a C# extension");
		} catch (Exception ex) {
			// Never let this escape: the runtime would abort the engine.
			Console.Error.WriteLine($"[my_plugin] register failed: {ex}");
		}
	}
}
