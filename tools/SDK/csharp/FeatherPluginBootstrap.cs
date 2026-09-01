// Everything a Feather plugin written in C# needs in order to be one.
//
// Copied in beside the generated bindings by the plugin SDK, so a plugin author
// writes none of it. What used to be hand-written per plugin -- a DllImport
// resolver, an [UnmanagedCallersOnly] export whose name had to match the .fext
// manifest, and a manual dispatch on the init level -- is here once, and the
// plugin is reduced to its own attributed types.
//
// The entry point below is a fixed name (feather_cs_plugin_entry), so every C#
// plugin's manifest names the same one and there is nothing to keep in sync.

using System;
using System.Collections.Generic;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

// Namespace, deliberately not under "Feather": the generated bindings put
// everything in a static partial *class* called Feather at global scope, so a
// namespace of that name would shadow it and every qualified reference inside
// the generated files (Feather.Const_Projection, and so on) would resolve to
// the namespace instead. The result is a wall of errors in generated code that
// has nothing wrong with it.
namespace FeatherPlugin {

/// <summary>Marks a class or struct as an ECS component type.</summary>
/// <remarks>
/// Its public instance fields become the component's fields, in declaration
/// order. Supported types: bool, int, float, double, Vector2, Vector3 and
/// Vector4 (a colour). The type is never instantiated -- it describes a layout,
/// and systems get a view onto the entity's real storage.
/// </remarks>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class FeatherComponentAttribute : Attribute {
	/// <summary>Registered name. Defaults to the type's own name.</summary>
	public string? Name { get; set; }
}

/// <summary>Marks a static method as a system over the named components.</summary>
/// <remarks>
/// The method must be <c>static void (ulong entity, ComponentView[] components,
/// double deltaTime)</c>. Components are named as strings, so a system can
/// query the engine's own components -- "Transform" -- as readily as one
/// declared here.
/// </remarks>
[AttributeUsage(AttributeTargets.Method)]
public sealed class FeatherSystemAttribute : Attribute {
	public FeatherSystemAttribute(params string[] components) {
		Components = components;
	}

	public string[] Components { get; }

	/// <summary>One of the pipeline phases; defaults to on_update.</summary>
	public string Phase { get; set; } = "on_update";

	/// <summary>Registered name. Defaults to the method's own name.</summary>
	public string? Name { get; set; }
}

/// <summary>Marks a static method to be called at each engine init level.</summary>
/// <remarks>Signature: <c>static void (byte level)</c>.</remarks>
[AttributeUsage(AttributeTargets.Method)]
public sealed class FeatherInitAttribute : Attribute {}

/// <summary>A view onto one component of one entity.</summary>
/// <remarks>
/// Reads and writes go straight to the entity's storage, so there is nothing to
/// copy back. Valid only for the duration of the call that produced it.
/// </remarks>
public readonly struct ComponentView {
	private readonly IntPtr _handle;
	private readonly string _component;

	internal ComponentView(IntPtr handle, string component) {
		_handle = handle;
		_component = component;
	}

	public bool IsValid => _handle != IntPtr.Zero;

	public bool GetBool(string field) => Read(field, 1)[0] != 0.0;
	public int GetInt(string field) => (int)Read(field, 1)[0];
	public float GetFloat(string field) => (float)Read(field, 1)[0];

	public Vector2 GetVector2(string field) {
		var v = Read(field, 2);
		return new Vector2((float)v[0], (float)v[1]);
	}

	public Vector3 GetVector3(string field) {
		var v = Read(field, 3);
		return new Vector3((float)v[0], (float)v[1], (float)v[2]);
	}

	public Vector4 GetColor(string field) {
		var v = Read(field, 4);
		return new Vector4((float)v[0], (float)v[1], (float)v[2], (float)v[3]);
	}

	public void SetBool(string field, bool value) => Write(field, new[] { value ? 1.0 : 0.0 });
	public void SetInt(string field, int value) => Write(field, new[] { (double)value });
	public void SetFloat(string field, float value) => Write(field, new[] { (double)value });
	public void SetVector2(string field, Vector2 value) => Write(field, new[] { (double)value.X, value.Y });
	public void SetVector3(string field, Vector3 value) =>
			Write(field, new[] { (double)value.X, value.Y, value.Z });
	public void SetColor(string field, Vector4 value) =>
			Write(field, new[] { (double)value.X, value.Y, value.Z, value.W });

	private double[] Read(string field, int expected) {
		var values = new double[4];
		int count;
		if (Native.GetField(_handle, _component, field, values, values.Length, out count) == 0) {
			throw new InvalidOperationException($"cannot read '{_component}.{field}'");
		}
		if (count != expected) {
			throw new InvalidOperationException(
					$"'{_component}.{field}' holds {count} value(s), not {expected}");
		}
		return values;
	}

	private void Write(string field, double[] values) {
		if (Native.SetField(_handle, _component, field, values, values.Length) == 0) {
			throw new InvalidOperationException($"cannot write '{_component}.{field}'");
		}
	}
}

/// <summary>Creating entities and reaching their components outside a system.</summary>
public static class World {
	public static ulong Spawn(params string[] components) => Spawn(null, components);

	public static ulong Spawn(string? name, params string[] components) {
		ulong entity = Native.CreateEntity(name ?? string.Empty);
		if (entity == 0) {
			throw new InvalidOperationException("could not create an entity (is the world up yet?)");
		}
		foreach (string component in components) {
			if (Native.AddComponent(entity, component) == 0) {
				throw new InvalidOperationException($"no component named '{component}'");
			}
		}
		return entity;
	}

	/// <summary>
	/// A view onto one of an entity's components. Invalidated by adding or
	/// removing components, which moves the entity's storage.
	/// </summary>
	public static ComponentView View(ulong entity, string component) {
		IntPtr handle = Native.ComponentHandle(entity, component);
		if (handle == IntPtr.Zero) {
			throw new InvalidOperationException($"entity {entity} has no '{component}'");
		}
		return new ComponentView(handle, component);
	}
}

internal static class Native {
	// The same logical name the generated bindings use. It names no file: the C
	// bindings are compiled into the engine executable, and the resolver below
	// maps the name onto the running process.
	private const string Library = "feather_c";

	[DllImport(Library, EntryPoint = "feather_script_define_component", ExactSpelling = true)]
	internal static extern ulong DefineComponent(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name,
			int fieldCount, IntPtr fieldNames, byte[] fieldTypes,
			byte[] error, int errorSize);

	[DllImport(Library, EntryPoint = "feather_script_define_system", ExactSpelling = true)]
	internal static extern unsafe ulong DefineSystem(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name,
			int componentCount, IntPtr componentNames, byte phase,
			delegate* unmanaged<IntPtr, ulong, IntPtr, int, double, void> callback,
			IntPtr userData, byte[] error, int errorSize);

	[DllImport(Library, EntryPoint = "feather_script_create_entity", ExactSpelling = true)]
	internal static extern ulong CreateEntity([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

	[DllImport(Library, EntryPoint = "feather_script_add_component", ExactSpelling = true)]
	internal static extern int AddComponent(ulong entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string component);

	[DllImport(Library, EntryPoint = "feather_script_component_handle", ExactSpelling = true)]
	internal static extern IntPtr ComponentHandle(ulong entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string component);

	[DllImport(Library, EntryPoint = "feather_script_get_field", ExactSpelling = true)]
	internal static extern int GetField(
			IntPtr handle,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string component,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string field,
			double[] values, int maxValues, out int count);

	[DllImport(Library, EntryPoint = "feather_script_set_field", ExactSpelling = true)]
	internal static extern int SetField(
			IntPtr handle,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string component,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string field,
			double[] values, int count);
}

/// <summary>Finds the plugin's types and registers them with the engine.</summary>
public static class Bootstrap {

	// Field type codes, matching FeatherScriptFieldType in
	// core/world/scripted_abi.h.
	private const byte FieldBool = 0, FieldInt = 1, FieldFloat = 2;
	private const byte FieldVec2 = 3, FieldVec3 = 4, FieldColor = 5;

	private static readonly string[] Phases = {
		"on_load", "post_load", "pre_update", "on_update",
		"on_validate", "post_update", "pre_store", "on_store",
	};

	// Handlers are kept for the life of the process: the engine holds a raw
	// pointer to the callback and calls it every frame, so nothing here may be
	// collected or moved.
	private static readonly List<MethodInfo> Systems = new();
	private static readonly List<string[]> SystemComponents = new();
	private static readonly List<MethodInfo> InitHooks = new();
	private static bool _registered;

	/// <summary>
	/// The engine's entry point, named by every C# plugin's .fext manifest.
	/// Called once per init level, ascending.
	/// </summary>
	[UnmanagedCallersOnly(EntryPoint = "feather_cs_plugin_entry")]
	public static void Entry(byte level) {
		// An exception escaping here cannot cross back into C -- the runtime
		// would abort with no message -- so everything is caught and reported.
		try {
			if (!_registered) {
				_registered = true;
				RegisterAll();
			}
			foreach (MethodInfo hook in InitHooks) {
				hook.Invoke(null, new object[] { level });
			}
		} catch (Exception ex) {
			Console.Error.WriteLine($"[feather] plugin registration failed: {ex}");
		}
	}

	private static void RegisterAll() {
		Type[] types = typeof(Bootstrap).Assembly.GetTypes();

		// Components first, all of them, before any system. A system names its
		// components as strings and they must already exist in the world to be
		// queried -- and GetTypes() is in no useful order, so a component
		// nested inside the class that declares a system over it comes second.
		foreach (Type type in types) {
			var component = type.GetCustomAttribute<FeatherComponentAttribute>();
			if (component != null) {
				Guarded($"component {component.Name ?? type.Name}", () => RegisterComponent(type, component));
			}
		}

		foreach (Type type in types) {
			foreach (MethodInfo method in type.GetMethods(BindingFlags.Static | BindingFlags.Public
					| BindingFlags.NonPublic | BindingFlags.DeclaredOnly)) {
				var system = method.GetCustomAttribute<FeatherSystemAttribute>();
				if (system != null) {
					Guarded($"system {system.Name ?? method.Name}", () => RegisterSystem(method, system));
				}
				if (method.GetCustomAttribute<FeatherInitAttribute>() != null) {
					InitHooks.Add(method);
				}
			}
		}
	}

	// Reported per item rather than per plugin: one component the engine cannot
	// store should not take the rest of the plugin -- including its init hooks --
	// down with it.
	private static void Guarded(string what, Action action) {
		try {
			action();
		} catch (Exception ex) {
			Console.Error.WriteLine($"[feather] could not register {what}: {ex.Message}");
		}
	}

	private static byte FieldTypeOf(Type type, string owner, string field) {
		if (type == typeof(bool)) return FieldBool;
		if (type == typeof(int)) return FieldInt;
		if (type == typeof(float) || type == typeof(double)) return FieldFloat;
		if (type == typeof(Vector2)) return FieldVec2;
		if (type == typeof(Vector3)) return FieldVec3;
		if (type == typeof(Vector4)) return FieldColor;
		throw new NotSupportedException(
				$"{owner}.{field}: {type.Name} is not a field type Feather can store "
				+ "(bool, int, float, double, Vector2, Vector3 or Vector4)");
	}

	private static void RegisterComponent(Type type, FeatherComponentAttribute attribute) {
		string name = attribute.Name ?? type.Name;
		FieldInfo[] fields = type.GetFields(BindingFlags.Public | BindingFlags.Instance);
		if (fields.Length == 0) {
			throw new InvalidOperationException($"component '{name}' declares no public fields");
		}

		var names = new string[fields.Length];
		var types = new byte[fields.Length];
		for (int i = 0; i < fields.Length; i++) {
			names[i] = fields[i].Name;
			types[i] = FieldTypeOf(fields[i].FieldType, name, fields[i].Name);
		}

		var error = new byte[512];
		using var block = new NativeStringArray(names);
		if (Native.DefineComponent(name, fields.Length, block.Pointer, types, error, error.Length) == 0) {
			throw new InvalidOperationException($"could not define component '{name}': {Decode(error)}");
		}
	}

	private static unsafe void RegisterSystem(MethodInfo method, FeatherSystemAttribute attribute) {
		string name = attribute.Name ?? method.Name;

		int phase = Array.IndexOf(Phases, attribute.Phase);
		if (phase < 0) {
			throw new ArgumentException(
					$"system '{name}': unknown phase '{attribute.Phase}' "
					+ $"(expected one of {string.Join(", ", Phases)})");
		}

		int index = Systems.Count;
		Systems.Add(method);
		SystemComponents.Add(attribute.Components);

		var error = new byte[512];
		using var block = new NativeStringArray(attribute.Components);
		ulong id = Native.DefineSystem(
				name, attribute.Components.Length, block.Pointer, (byte)phase,
				&Dispatch, (IntPtr)index, error, error.Length);
		if (id == 0) {
			throw new InvalidOperationException($"could not define system '{name}': {Decode(error)}");
		}
	}

	// The single native callback every scripted system routes through; the
	// index it was registered with says which managed method to run.
	[UnmanagedCallersOnly]
	private static void Dispatch(IntPtr userData, ulong entity, IntPtr components, int count, double deltaTime) {
		try {
			int index = (int)userData;
			MethodInfo method = Systems[index];
			string[] names = SystemComponents[index];

			var views = new ComponentView[count];
			for (int i = 0; i < count; i++) {
				IntPtr handle = Marshal.ReadIntPtr(components, i * IntPtr.Size);
				views[i] = new ComponentView(handle, names[i]);
			}

			method.Invoke(null, new object[] { entity, views, deltaTime });
		} catch (Exception ex) {
			// Same rule as the entry point: nothing may propagate into C.
			Console.Error.WriteLine($"[feather] system callback failed: {ex}");
		}
	}

	private static string Decode(byte[] error) {
		int length = Array.IndexOf(error, (byte)0);
		return Encoding().GetString(error, 0, length < 0 ? error.Length : length);
	}

	private static System.Text.Encoding Encoding() => System.Text.Encoding.UTF8;

	// A null-terminated array of UTF-8 strings, freed when the call is done.
	private sealed class NativeStringArray : IDisposable {
		private readonly IntPtr[] _strings;
		private readonly IntPtr _array;

		public NativeStringArray(string[] values) {
			_strings = new IntPtr[values.Length];
			for (int i = 0; i < values.Length; i++) {
				_strings[i] = Marshal.StringToCoTaskMemUTF8(values[i]);
			}
			_array = Marshal.AllocCoTaskMem(IntPtr.Size * values.Length);
			for (int i = 0; i < values.Length; i++) {
				Marshal.WriteIntPtr(_array, i * IntPtr.Size, _strings[i]);
			}
		}

		public IntPtr Pointer => _array;

		public void Dispose() {
			foreach (IntPtr s in _strings) {
				Marshal.FreeCoTaskMem(s);
			}
			Marshal.FreeCoTaskMem(_array);
		}
	}
}

internal static class Resolver {
	// The generated [DllImport]s, and this file's own, name "feather_c" with no
	// path. .NET would turn that into a search for a file of that name; there is
	// none, because the bindings are compiled into the engine executable. The
	// main program's handle is the answer -- it is the module that exports them.
	//
	// Resolving explicitly also turns a lookup failure into a readable message
	// rather than an abort: an exception escaping an UnmanagedCallersOnly entry
	// point cannot cross back into C.
	[ModuleInitializer]
	internal static void Install() {
		NativeLibrary.SetDllImportResolver(typeof(Resolver).Assembly, static (name, assembly, searchPath) => {
			if (name != "feather_c") {
				return IntPtr.Zero;
			}
			IntPtr main = NativeLibrary.GetMainProgramHandle();
			if (NativeLibrary.TryGetExport(main, "feather_script_define_component", out _)) {
				return main;
			}
			Console.Error.WriteLine(
					"[feather] the host process exports no Feather C bindings; "
					+ "is this running inside the engine?");
			return IntPtr.Zero;
		});
	}
}

} // namespace FeatherPlugin
