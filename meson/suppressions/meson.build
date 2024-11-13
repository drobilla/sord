# Copyright 2020-2024 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

# Project-specific warning suppressions

warning_level = get_option('warning_level')

#####
# C #
#####

c_suppressions = []

if cc.get_id() in ['clang', 'emscripten']
  if warning_level == 'everything'
    c_suppressions += [
      '-Wno-cast-align',
      '-Wno-cast-function-type-strict',
      '-Wno-cast-qual',
      '-Wno-declaration-after-statement',
      '-Wno-double-promotion',
      '-Wno-format-nonliteral',
      '-Wno-padded',
      '-Wno-reserved-id-macro',
      '-Wno-switch-default',
      '-Wno-switch-enum',
      '-Wno-unsafe-buffer-usage',
      '-Wno-unused-macros',
    ]

    if not meson.is_cross_build()
      c_suppressions += [
        '-Wno-poison-system-directories',
      ]
    endif
  endif

  if warning_level in ['everything', '3']
    c_suppressions += [
      '-Wno-nullability-extension',
    ]

    if host_machine.system() == 'freebsd'
      c_suppressions += [
        '-Wno-c11-extensions',
      ]
    endif

    if host_machine.system() == 'windows'
      c_suppressions += [
        '-Wno-deprecated-declarations',
        '-Wno-nonportable-system-include-path',
      ]
    endif
  endif

elif cc.get_id() == 'gcc'
  if warning_level == 'everything'
    c_suppressions += [
      '-Wno-cast-align',
      '-Wno-cast-qual',
      '-Wno-format-nonliteral',
      '-Wno-inline',
      '-Wno-padded',
      '-Wno-strict-overflow',
      '-Wno-suggest-attribute=const',
      '-Wno-suggest-attribute=pure',
      '-Wno-switch-default',
      '-Wno-switch-enum',
      '-Wno-unsuffixed-float-constants',
      '-Wno-unused-const-variable',
      '-Wno-unused-macros',
    ]
  endif

  if warning_level in ['everything', '3']
    c_suppressions += [
      '-Wno-format',
    ]
  endif

  if host_machine.system() == 'windows'
    c_suppressions += [
      '-Wno-float-conversion',
      '-Wno-suggest-attribute=format',
    ]
  endif

elif cc.get_id() == 'msvc'
  c_suppressions += [
    '/experimental:external',
    '/external:W0',
    '/external:anglebrackets',
  ]

  if warning_level == 'everything'
    c_suppressions += [
      '/wd4061', # enumerator in switch is not explicitly handled
      '/wd4200', # zero-sized array in struct/union
      '/wd4514', # unreferenced inline function has been removed
      '/wd4710', # function not inlined
      '/wd4711', # function selected for automatic inline expansion
      '/wd4800', # implicit conversion from int to bool
      '/wd4820', # padding added after construct
      '/wd4996', # POSIX name for this item is deprecated
      '/wd5045', # will insert Spectre mitigation for memory load
      '/wd5264', # const variable is not used
    ]
  endif

  if warning_level in ['everything', '3']
    c_suppressions += [
      '/wd4706', # assignment within conditional expression
    ]
  endif

  if warning_level in ['everything', '3', '2']
    c_suppressions += [
      '/wd4996', # function or variable may be unsafe
    ]
  endif
endif

c_suppressions = cc.get_supported_arguments(c_suppressions)
