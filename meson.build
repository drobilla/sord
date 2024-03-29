# Copyright 2021-2024 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

project(
  'sord',
  ['c'],
  default_options: [
    'b_ndebug=if-release',
    'buildtype=release',
    'c_std=c99',
  ],
  license: 'ISC',
  meson_version: '>= 0.56.0',
  version: '0.16.17',
)

sord_src_root = meson.current_source_dir()
major_version = meson.project_version().split('.')[0]
version_suffix = '-@0@'.format(major_version)
versioned_name = 'sord' + version_suffix

#######################
# Compilers and Flags #
#######################

# Required tools
pkg = import('pkgconfig')
cc = meson.get_compiler('c')

# Set global warning flags
subdir('meson/suppressions')

# Build as C++ with MSVC
if cc.get_id() == 'msvc'
  add_project_arguments(['/TP'], language: ['c'])
endif

################
# Dependencies #
################

m_dep = cc.find_library('m', required: false)

zix_dep = dependency('zix-0', version: '>= 0.4.0')
serd_dep = dependency('serd-0', version: '>= 0.30.10')

##########################
# Platform Configuration #
##########################

# Use versioned name everywhere to support parallel major version installations
if host_machine.system() == 'windows'
  if get_option('default_library') == 'both'
    error('default_library=both is not supported on Windows')
  endif
  soversion = ''
else
  soversion = meson.project_version().split('.')[0]
endif

###########
# Library #
###########

c_headers = files('include/sord/sord.h')
cpp_headers = files('include/sord/sordmm.hpp')

sources = files(
  'src/sord.c',
  'src/syntax.c',
)

# Set appropriate arguments for building against the library type
extra_c_args = []
if get_option('default_library') == 'static'
  extra_c_args = ['-DSORD_STATIC']
endif

# Build shared and/or static library
libsord = library(
  versioned_name,
  sources,
  c_args: c_suppressions + extra_c_args + [
    '-DSORD_INTERNAL',
    '-DSORD_VERSION="@0@"'.format(meson.project_version()),
  ],
  darwin_versions: [major_version + '.0.0', meson.project_version()],
  dependencies: [m_dep, zix_dep, serd_dep],
  gnu_symbol_visibility: 'hidden',
  include_directories: include_directories('include', 'src'),
  install: true,
  soversion: soversion,
  version: meson.project_version(),
)

# Declare dependency for internal meson dependants
sord_dep = declare_dependency(
  compile_args: extra_c_args,
  dependencies: [m_dep, zix_dep, serd_dep],
  include_directories: include_directories('include'),
  link_with: libsord,
)

# Generage pkg-config file for external dependants
pkg.generate(
  libsord,
  description: 'Lightweight C library for storing RDF in memory',
  extra_cflags: extra_c_args,
  filebase: versioned_name,
  name: 'Sord',
  requires: ['serd-0'],
  subdirs: [versioned_name],
  version: meson.project_version(),
)

# Override pkg-config dependency for internal meson dependants
meson.override_dependency(versioned_name, sord_dep)

# Install headers to a versioned include directory
install_headers(c_headers, subdir: versioned_name / 'sord')
install_headers(cpp_headers, subdir: versioned_name / 'sord')

#########
# Tools #
#########

# Build sordi command line utility
if not get_option('tools').disabled()
  sordi = executable(
    'sordi',
    files('src/sordi.c'),
    c_args: c_suppressions,
    install: true,
    dependencies: sord_dep,
  )

  meson.override_find_program('sordi', sordi)

  pcre2_dep = dependency('libpcre2-8', include_type: 'system', required: false)

  if pcre2_dep.found()
    sord_validate = executable(
      'sord_validate',
      files('src/sord_validate.c'),
      c_args: c_suppressions,
      install: true,
      dependencies: [sord_dep, pcre2_dep],
    )

    meson.override_find_program('sord_validate', sord_validate)
  endif

  if not get_option('man').disabled()
    install_man(files('doc/sord_validate.1', 'doc/sordi.1'))
  endif
endif

# Display top-level summary (before subdirectories to appear first)
if not meson.is_subproject()
  summary(
    {
      'Tests': not get_option('tests').disabled(),
      'Tools': not get_option('tools').disabled(),
    },
    bool_yn: true,
    section: 'Components',
  )

  summary(
    {
      'Install prefix': get_option('prefix'),
      'Headers': get_option('prefix') / get_option('includedir'),
      'Libraries': get_option('prefix') / get_option('libdir'),
    },
    section: 'Directories',
  )

  if not get_option('tools').disabled()
    summary(
      {
        'Executables': get_option('prefix') / get_option('bindir'),
        'Man pages': get_option('prefix') / get_option('mandir'),
      },
      section: 'Directories',
    )
  endif
endif

###########
# Support #
###########

if not get_option('tests').disabled()
  subdir('test')
endif

subdir('doc')
