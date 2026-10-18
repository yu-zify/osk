project('osk', 'c',
  default_options: ['c_std=c11', 'warning_level=2'])

add_project_arguments('-D_GNU_SOURCE', language: 'c')

inc = include_directories('src')

gtk_dep = dependency('gtk+-3.0')

executable('osk',
  sources: [
    'src/main.c',
    'src/app.c',
    'src/backend/uinput_keyboard.c',
    'src/backend/uinput_touchpad.c',        # 新增
    'src/keylib/keydefs.c',
    'src/widgets/keybutton.c',
    'src/widgets/touchpad.c',               # 新增
    'src/layout/layout.c',
    'src/layout/util.c',
    'src/layout/layout_standard.c',
    'src/layout/layout_minimal.c',
    'src/layout/layout_fullscreen.c',
    'src/layout/layout_touchpad.c',
    'src/layout/layout_game.c',         # 新增
  ],
  include_directories: inc,
  dependencies: [gtk_dep],
  install: false)