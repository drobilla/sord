#!/usr/bin/env python
import os

from waflib.extras import autowaf as autowaf
import waflib.Logs as Logs, waflib.Options as Options

# Version of this package (even if built as a child)
SORD_VERSION = '0.1.0'

# Library version (UNIX style major, minor, micro)
# major increment <=> incompatible changes
# minor increment <=> compatible changes (additions)
# micro increment <=> no interface changes
# Sord uses the same version number for both library and package
SORD_LIB_VERSION = SORD_VERSION

# Variables for 'waf dist'
APPNAME = 'sord'
VERSION = SORD_VERSION

# Mandatory variables
top = '.'
out = 'build'

def options(opt):
    autowaf.set_options(opt)
    opt.add_option('--test', action='store_true', default=False, dest='build_tests',
                   help="Build unit tests")
    opt.add_option('--dump', type='string', default='', dest='dump',
                   help="Dump debugging output (iter, search, write, all)")

def configure(conf):
    autowaf.configure(conf)
    autowaf.display_header('Sord configuration')

    conf.load('compiler_cc')
    conf.env.append_value('CFLAGS', '-std=c99')

    autowaf.check_pkg(conf, 'glib-2.0', uselib_store='GLIB',
                      atleast_version='2.0.0', mandatory=True)

    autowaf.check_pkg(conf, 'serd', uselib_store='SERD',
                      atleast_version='0.1.0', mandatory=True)

    conf.env['BUILD_TESTS'] = Options.options.build_tests
    conf.env['BUILD_UTILS'] = True

    dump = Options.options.dump.split(',')
    all = 'all' in dump
    if all or 'iter' in dump:
        autowaf.define(conf, 'SORD_DEBUG_ITER', 1)
    if all or 'search' in dump:
        autowaf.define(conf, 'SORD_DEBUG_SEARCH', 1)
    if all or 'write' in dump:
        autowaf.define(conf, 'SORD_DEBUG_WRITE', 1)

    autowaf.define(conf, 'SORD_VERSION', SORD_VERSION)
    conf.write_config_header('sord-config.h', remove=False)

    autowaf.display_msg(conf, "Utilities", str(conf.env['BUILD_UTILS']))
    autowaf.display_msg(conf, "Unit tests", str(conf.env['BUILD_TESTS']))
    autowaf.display_msg(conf, "Debug dumping", dump)
    print('')

def build(bld):
    # C/C++ Headers
    bld.install_files('${INCLUDEDIR}/sord', bld.path.ant_glob('sord/*.h'))
    bld.install_files('${INCLUDEDIR}/sord', bld.path.ant_glob('sord/*.hpp'))

    # Pkgconfig file
    autowaf.build_pc(bld, 'SORD', SORD_VERSION, [])

    # Library
    obj = bld(features        = 'c cshlib',
              source          = 'src/sord.c src/syntax.c',
              includes        = ['.', './src'],
              export_includes = ['.'],
              name            = 'libsord',
              target          = 'sord',
              vnum            = SORD_LIB_VERSION,
              install_path    = '${LIBDIR}',
              libs            = [ 'm' ],
              cflags          = [ '-fvisibility=hidden',
                                  '-DSORD_SHARED',
                                  '-DSORD_INTERNAL' ])
    autowaf.use_lib(bld, obj, 'GLIB SERD')

    if bld.env['BUILD_TESTS']:
        test_cflags = [ '-fprofile-arcs',  '-ftest-coverage' ],

        # Static library (for unit test code coverage)
        obj = bld(features     = 'c cstlib',
                  source       = 'src/sord.c src/syntax.c',
                  includes     = ['.', './src'],
                  name         = 'libsord_static',
                  target       = 'sord_static',
                  install_path = '',
                  cflags       = test_cflags,
                  libs         = [ 'm' ])
        autowaf.use_lib(bld, obj, 'GLIB SERD')

        # Unit test program
        obj = bld(features     = 'c cprogram',
                  source       = 'src/sord_test.c',
                  includes     = ['.', './src'],
                  use          = 'libsord_static',
                  linkflags    = '-lgcov',
                  target       = 'sord_test',
                  install_path = '',
                  cflags       = test_cflags)
        autowaf.use_lib(bld, obj, 'GLIB SERD')

        # Unit test programa
        if bld.env['BUILD_UTILS']:
            obj = bld(features     = 'c cprogram',
                      source       = 'src/sordi.c',
                      includes     = ['.', './src'],
                      use          = 'libsord_static',
                      linkflags    = '-lgcov',
                      target       = 'sordi_static',
                      install_path = '',
                      cflags       = test_cflags)

    # Documentation
    autowaf.build_dox(bld, 'SORD', SORD_VERSION, top, out)

    bld.add_post_fun(autowaf.run_ldconfig)

def fix_docs(ctx):
    try:
        os.chdir('build/doc/html')
        os.system("sed -i 's/SORD_API //' group__sord.html")
        os.system("sed -i 's/SORD_DEPRECATED //' group__sord.html")
        os.remove('index.html')
        os.symlink('group__sord.html',
                   'index.html')
    except Exception as e:
        Logs.error("Failed to fix up Doxygen documentation (%s)\n" % e)

def upload_docs(ctx):
    os.system("rsync -avz --delete -e ssh build/doc/html/* drobilla@drobilla.net:~/drobilla.net/docs/sord")

def test(ctx):
    autowaf.pre_test(ctx, APPNAME)
    autowaf.run_tests(ctx, APPNAME, ['./sord_test'])
    autowaf.post_test(ctx, APPNAME)
