{
  "targets": [
    {
      "target_name": "vock",
      "dependencies": ["deps/opus/opus.gyp:opus"],
      "include_dirs": ["src", "src/audio", "deps/opus/opus/include"],

      "sources": [
        "src/node_opus.cc",
        "src/audio/au.cc",
        "src/audio/core.cc",
        "src/vock.cc"
      ],
      "conditions": [
        ["OS=='mac'", {
          "libraries": [ "-framework AudioUnit" ],
        }]
      ]
    }
  ]
}
