Put the engine's published API description here:

    cd <FeatherEngine>
    xmake export-api
    cp build/bindings/dist/feather_api.json <this directory>

It belongs in version control. The engine rewrites its own checkout path and
DirectXMath's out of the file before publishing it, replacing them with the
`@feather` and `@directxmath` tokens the SDK substitutes back, so the file
describes the API and nothing about the machine that produced it.

Nothing here reads the engine's source. Regenerate this file when you move to an
engine build with a changed API.
