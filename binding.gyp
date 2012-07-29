{
  "targets": [
    {
      "target_name": "vock",
      "dependencies": [
        "deps/opus/opus.gyp:opus",
        "deps/speex/speex.gyp:speex",
      ],

      "include_dirs": [
        "src",
        "src/audio",
        "deps/opus/opus/include",
        "deps/speex/speex/include",
      ],

      "sources": [
        "src/node_opus.cc",
        "src/audio/portaudio/pa_ringbuffer.c",
        "src/audio/au.cc",
        "src/audio/core.cc",
        "src/vock.cc",
      ],
      "conditions": [
        ["OS=='mac'", {
          "libraries": [ "-framework AudioUnit" ],
        }]
      ]
    }
  ]
}
