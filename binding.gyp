{
  "targets": [
    {
      "target_name": "libopus",
      "dependencies": ["deps/opus/opus.gyp:opus"],
      "include_dirs": ["src", "deps/opus/opus/include"],

      "sources": [
        "src/node_opus.cc"
      ]
    }
  ]
}
