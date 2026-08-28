Put the engine's published API description here:

    cd <FeatherEngine>
    xmake export-api
    cp build/bindings/dist/feather_api.json build/bindings/dist/feather_api.meta.json <this directory>

Both files belong in version control. `feather_api.json` is what the bindings
are generated from; `feather_api.meta.json` records which engine build and which
mrbind revision produced it, so a mismatch is reported rather than silently
producing headers that disagree with the engine you load into.

Nothing here reads the engine's source. Regenerate these two files when you move
to an engine build with a changed API.
