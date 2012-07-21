{
  "targets": [
    {
      "target_name": "libopus",
      "dependencies": ["deps/opus/opus.gyp:opus"],
      "include_dirs": ["src", "deps/opus/include"],

      "sources": [
        "src/libopus.cc"
      ]
    }
  ]
}
