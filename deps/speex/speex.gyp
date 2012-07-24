{
  "targets": [
    {
      "target_name": "speex",
      "type": "static_library",
      "defines": ["HAVE_CONFIG_H"],
      "include_dirs": [
        "",
        "speex/include",
        "speex/src",
        "speex/libspeex",
      ],
      "sources": [
        "speex/libspeex/cb_search.c",
        "speex/libspeex/exc_10_32_table.c",
        "speex/libspeex/exc_8_128_table.c",
        "speex/libspeex/filters.c",
        "speex/libspeex/gain_table.c",
        "speex/libspeex/hexc_table.c",
        "speex/libspeex/high_lsp_tables.c",
        "speex/libspeex/lsp.c",
        "speex/libspeex/ltp.c",
        "speex/libspeex/speex.c",
        "speex/libspeex/stereo.c",
        "speex/libspeex/vbr.c",
        "speex/libspeex/vq.c",
        "speex/libspeex/bits.c",
        "speex/libspeex/exc_10_16_table.c",
        "speex/libspeex/exc_20_32_table.c",
        "speex/libspeex/exc_5_256_table.c",
        "speex/libspeex/exc_5_64_table.c",
        "speex/libspeex/gain_table_lbr.c",
        "speex/libspeex/hexc_10_32_table.c",
        "speex/libspeex/lpc.c",
        "speex/libspeex/lsp_tables_nb.c",
        "speex/libspeex/modes.c",
        "speex/libspeex/modes_wb.c",
        "speex/libspeex/nb_celp.c",
        "speex/libspeex/quant_lsp.c",
        "speex/libspeex/sb_celp.c",
        "speex/libspeex/speex_callbacks.c",
        "speex/libspeex/speex_header.c",
        "speex/libspeex/window.c",
        "speex/libspeex/preprocess.c",
        "speex/libspeex/jitter.c",
        "speex/libspeex/mdf.c",
        "speex/libspeex/fftwrap.c",
        "speex/libspeex/filterbank.c",
        "speex/libspeex/resample.c",
        "speex/libspeex/buffer.c",
        "speex/libspeex/scal.c",
        "speex/libspeex/kiss_fft.c",
        "speex/libspeex/kiss_fftr.c",

      ],
      "conditions": [
        ['OS=="mac"', {
          'xcode_settings': {
            'ALWAYS_SEARCH_USER_PATHS': 'NO',
            'GCC_CW_ASM_SYNTAX': 'NO',                # No -fasm-blocks
            'GCC_DYNAMIC_NO_PIC': 'NO',               # No -mdynamic-no-pic
                                                      # (Equivalent to -fPIC)
            'GCC_ENABLE_CPP_EXCEPTIONS': 'NO',        # -fno-exceptions
            'GCC_ENABLE_CPP_RTTI': 'NO',              # -fno-rtti
            'GCC_ENABLE_PASCAL_STRINGS': 'NO',        # No -mpascal-strings
            'GCC_THREADSAFE_STATICS': 'NO',           # -fno-threadsafe-statics
            'GCC_VERSION': '4.2',
            'GCC_WARN_ABOUT_MISSING_NEWLINE': 'YES',  # -Wnewline-eof
            'PREBINDING': 'NO',                       # No -Wl,-prebind
            'MACOSX_DEPLOYMENT_TARGET': '10.5',       # -mmacosx-version-min=10.5
            'USE_HEADERMAP': 'NO',
            'WARNING_CFLAGS': [
              '-Wall',
              '-Wendif-labels',
              '-W',
              '-Wno-unused-parameter',
            ],
          },
        }],
      ]
    }
  ]
}
