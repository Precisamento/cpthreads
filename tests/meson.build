check_location = get_option('check_location')

if check_location != ''
    check_lib = check_location + '/lib'
    
    compat = cc.find_library('compat',
        required: true,
        dirs: check_lib)
    
    check = cc.find_library('check',
        required: true,
        dirs: check_lib)
    
    deps = [compat, check]
    inc = include_directories([check_location + '/include'])
    
    test_link_args = []
    
    if get_option('buildtype') == 'release'
        test_link_args += '/NODEFAULTLIB:MSVCRTD'
    endif
    
    thread_test = executable('thread_test',
        'thread_tests.c',
        link_with: cpthreads,
        link_args: test_link_args,
        include_directories: inc,
        dependencies: deps
    )
    
    mutex_test = executable('mutex_test',
        'mutex_tests.c',
        link_with: cpthreads,
        link_args: test_link_args,
        include_directories: inc,
        dependencies: deps
    )

    tss_test = executable('tss_test',
        'tss_tests.c',
        link_with: cpthreads,
        link_args: test_link_args,
        include_directories: inc,
        dependencies: deps
    )

    cnd_test = executable('cnd_test',
        'cnd_tests.c',
        link_with: cpthreads,
        link_args: test_link_args,
        include_directories: inc,
        dependencies: deps
    )
    
    test('Thread Test', thread_test)
    test('Mutex Test', mutex_test)
    test('TSS Test', tss_test)
    test('Condition Test', cnd_test)
endif

