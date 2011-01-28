#!/usr/bin/env python
import autowaf
import Options

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

	conf.check_tool('compiler_cc')
	conf.env.append_value('CCFLAGS', '-std=c99')

	autowaf.check_pkg(conf, 'glib-2.0', uselib_store='GLIB',
	                  atleast_version='2.0.0', mandatory=True)

	autowaf.check_pkg(conf, 'serd', uselib_store='SERD',
	                  atleast_version='0.1.0', mandatory=False)

	conf.env['BUILD_TESTS'] = Options.options.build_tests
	conf.env['BUILD_UTILS'] = conf.env['HAVE_SERD'] != 0

	dump = Options.options.dump.split(',')
	all = 'all' in dump
	if all or 'iter' in dump:
		conf.define('SORD_DEBUG_ITER', 1)
	if all or 'search' in dump:
		conf.define('SORD_DEBUG_SEARCH', 1)
	if all or 'write' in dump:
		conf.define('SORD_DEBUG_WRITE', 1)

	conf.define('SORD_VERSION', SORD_VERSION)
	conf.write_config_header('sord-config.h')

	autowaf.display_msg(conf, "Utilities", str(conf.env['BUILD_UTILS']))
	autowaf.display_msg(conf, "Unit tests", str(conf.env['BUILD_TESTS']))
	autowaf.display_msg(conf, "Debug dumping", dump)
	print

def build(bld):
	# C Headers
	bld.install_files('${INCLUDEDIR}/sord', 'sord/*.h')

	# Pkgconfig file
	autowaf.build_pc(bld, 'SORD', SORD_VERSION, [])

	# Library
	obj = bld(features = 'c cshlib')
	obj.source       = 'src/sord.c src/syntax.c'
	obj.includes     = ['.', './src']
	obj.name         = 'libsord'
	obj.target       = 'sord'
	obj.vnum         = SORD_LIB_VERSION
	obj.install_path = '${LIBDIR}'
	obj.cflags       = [ '-fvisibility=hidden', '-DSORD_SHARED', '-DSORD_INTERNAL' ]
	obj.libs         = [ 'm' ]
	autowaf.use_lib(bld, obj, 'GLIB')
	
	if bld.env['BUILD_TESTS']:
		# Static library (for unit test code coverage)
		obj = bld(features = 'c cstlib')
		obj.source       = 'src/sord.c src/syntax.c'
		obj.includes     = ['.', './src']
		obj.name         = 'libsord_static'
		obj.target       = 'sord_static'
		obj.install_path = ''
		obj.cflags       = [ '-fprofile-arcs',  '-ftest-coverage' ]
		obj.libs         = [ 'm' ]
		autowaf.use_lib(bld, obj, 'GLIB')

		# Unit test program
		obj = bld(features = 'c cprogram')
		obj.source       = 'src/sord_test.c'
		obj.includes     = ['.', './src']
		obj.use          = 'libsord_static'
		obj.linkflags    = '-lgcov'
		obj.target       = 'sord_test'
		obj.install_path = ''
		obj.cflags       = [ '-fprofile-arcs',  '-ftest-coverage' ]
		autowaf.use_lib(bld, obj, 'GLIB')

		# Unit test programa
		if bld.env['BUILD_UTILS']:
			obj = bld(features = 'c cprogram')
			obj.source       = 'src/sordi.c'
			obj.includes     = ['.', './src']
			obj.use          = 'libsord_static'
			obj.linkflags    = '-lgcov'
			obj.target       = 'sordi_static'
			obj.install_path = ''
			obj.cflags       = [ '-fprofile-arcs',  '-ftest-coverage' ]
			autowaf.use_lib(bld, obj, 'SERD')

	# Documentation
	autowaf.build_dox(bld, 'SORD', SORD_VERSION, top, out)
	
def test(ctx):
	autowaf.pre_test(ctx, APPNAME)
	autowaf.run_tests(ctx, APPNAME, ['./sord_test'])
	autowaf.post_test(ctx, APPNAME)
