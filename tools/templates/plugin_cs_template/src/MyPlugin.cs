// TODO: rename this file, the class, and the names in my_plugin.fext.
//
// Note what is *not* here: no entry point, no DllImport resolver, no dispatch
// on the init level. The SDK's bootstrap (sdk/csharp/FeatherPluginBootstrap.cs,
// copied in alongside the generated bindings) supplies all three and finds the
// types below by reflecting over this assembly. my_plugin.fext's "entry" names
// the bootstrap's own fixed entry point, not one this file exports -- every C#
// plugin's manifest names the same one.

using System;
using System.Numerics;
using FeatherPlugin;

internal static class MyPlugin {

	// An ECS component: a type with public fields, describing a layout. Never instantiated -- systems get a view onto the
	// entity's real storage. Supported field types: bool, int, float, double, Vector2, Vector3, Vector4 (a colour).
	[FeatherComponent]
	private struct Wobble {
		public float Speed;
		public int Ticks;
	}

	// A system over it, run every frame at the named phase. Components are named as strings, so a system can query the
	// engine's own components -- "Transform" -- as readily as one declared here.
	[FeatherSystem("Wobble", Phase = "on_update")]
	private static void Advance(ulong entity, ComponentView[] components, double deltaTime) {
		ComponentView wobble = components[0];
		wobble.SetInt("Ticks", wobble.GetInt("Ticks") + 1);
	}

	// Called once per init level the engine enters, ascending. Mirrors
	// feather::InitLevel, which crosses the boundary as a uint8_t; Core is 0.
	[FeatherInit]
	private static void OnInitLevel(byte level) {
		if (level != 0) {
			return;
		}

		Console.WriteLine("[my_plugin] hello from a C# extension");

		// Spawn an entity carrying the component above, so Advance has something to match. Remove this and the component/system
		// if your plugin only needs to call the engine, the way the C example does.
		World.Spawn("WobbleDemo", "Wobble");
	}
}
