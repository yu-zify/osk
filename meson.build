project('osk', 'c',
  default_options: ['c_std=c11', 'warning_level=2'])
  
cc = meson.get_compiler('c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

inc = include_directories('src')

gtk_dep = dependency('gtk+-3.0')
m_dep = cc.find_library('m', required: true)

executable('osk',
  sources: [
    'src/main.c',
    'src/app.c',
    'src/config.c',
    'src/first_run.c',
    'src/settings.c',
    'src/backend/uinput_keyboard.c',
    'src/backend/uinput_mouse.c',
    'src/backend/uinput_gamepad.c',
    'src/keylib/keydefs.c',
    'src/widgets/keybutton.c',
    'src/widgets/touchpad.c',
    'src/widgets/stick.c',
    'src/layout/layout.c',
    'src/layout/util.c',
    'src/layout/layout_standard.c',
    'src/layout/layout_minimal.c',
    'src/layout/layout_game.c',
    'src/layout/gamepad_model.c',
    'src/layout/gamepad_state.c',
    'src/layout/gamepad_editor.c',
  ],
  include_directories: inc,
  dependencies: [gtk_dep,m_dep],
  install: false)

# 仅在用户运行 meson test 时执行；不访问设备、不提权、不改实际配置。
first_run_test = executable('first-run-test',
  'tests/first_run_test.c',
  include_directories: inc,
  dependencies: [gtk_dep])
test('first-run-config', first_run_test)