project('acrn', 'c', version: '3.4-unstable')

cc = meson.get_compiler('c')
crypt_dep = cc.find_library('crypto')
pthread_dep = cc.find_library('pthread')
rt_dep = cc.find_library('rt')

glib_dep = dependency('glib-2.0')
libdrm_dep = dependency('libdrm')
libusb_dep = dependency('libusb-1.0')
pixman_dep = dependency('pixman-1')
sdl2_dep = dependency('sdl2')
egl_dep = dependency('egl')
gles2_dep = dependency('glesv2')
uring_dep = dependency('liburing')
pciaccess_dep = dependency('pciaccess')
cjson_dep = dependency('libcjson')
asound_dep = dependency('alsa')
blkid_dep = dependency('blkid')
udev_dep = dependency('libudev')


config_data = configuration_data()
config_data.set('ACRN_VERSION', meson.project_version())

remote_branch = run_command('git', ['rev-parse', '--abbrev-ref', 'HEAD']).stdout().strip()
config_data.set('REMOTE_BRANCH', remote_branch)

commit = run_command('git', ['rev-parse',  '--verify', '--short', 'HEAD']).stdout().strip()
dirty = run_command('git', ['diff-index', '--name-only', 'HEAD']).stdout().strip()
if dirty == ''
  patch = commit
else
  patch = commit + '-dirty'
endif
config_data.set('PATCH', patch)

commit_tag = run_command('git', ['tag', '--points-at', 'HEAD']).stdout().strip()
commit_time = run_command('git', ['log', '-1', '--date=format:%Y-%m-%d-%T', '--format=%cd']).stdout().strip()
build_time = run_command('date', ['-u', '+%Y-%m-%d %H:%M:%S']).stdout().strip()
build_user = run_command('id', ['-u', '-n']).stdout().strip()
config_data.set('COMMIT_TAGS', commit_tag)
config_data.set('COMMIT_TIME', commit_time)
config_data.set('BUILD_TIME', build_time)
config_data.set('BUILD_USER', build_user)

configure_file(
  input: 'acrn_config.h.in',
  output: 'acrn_config.h',
  configuration: config_data,
)

common_cflags = [
    '-g',
    '-O2',
    '-std=gnu11',
    '-D_GNU_SOURCE',
    '-DNO_OPENSSL',
    '-m64',
    '-Wall',
    '-Wimplicit-fallthrough',
    '-Wformat',
    '-Wformat-security',
    '-Wno-stringop-truncation',
    '-Wno-address-of-packed-member',
    '-fstack-protector-strong',
    '-fno-strict-aliasing',
    '-fno-delete-null-pointer-checks',
    '-fwrapv',
    '-pie',
    '-Werror',
    '-ffunction-sections',
    '-U_FORTIFY_SOURCE',
    '-D_FORTIFY_SOURCE=2',
]

top_inc = include_directories('.')

dm_inc = include_directories([
    'devicemodel/include',
    'devicemodel/include/public',
])

subdir('misc')
subdir('devicemodel')
