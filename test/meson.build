unit_tests = [
  'sord',
]

foreach unit : unit_tests
  test(
    unit,
    executable(
      'test_@0@'.format(unit),
      files('test_@0@.c'.format(unit)),
      c_args: c_suppressions,
      dependencies: sord_dep,
    ),
    suite: 'unit',
  )
endforeach

if get_option('strict') and not meson.is_subproject()
  # Check release metadata
  autoship = find_program('autoship', required: false)
  if autoship.found()
    test('autoship', autoship, args: ['test', sord_src_root], suite: 'data')
  endif
endif
