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
        "src/opus",
        "src/audio",
        "deps/opus/opus/include",
        "deps/speex/speex/include",
      ],

      "libraries": [ "-lpthread" ],

      "sources": [
        "src/opus/binding.cc",
        "src/audio/portaudio/pa_ringbuffer.c",
        "src/audio/platform/mac.c",
        "src/audio/unit.cc",
        "src/audio/binding.cc",
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
