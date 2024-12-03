
libacrn_mngr_pub_inc = include_directories('.')
libacrn_mngr = static_library('libacrn_mngr',
  'acrn_mngr.c',
  include_directories: [
    libacrn_mngr_pub_inc,
    dm_inc,
  ],
  c_args: [
    common_cflags,
  ],
)

executable('acrnctl',
  sources: [
    'acrnctl.c',
    'acrn_vm_ops.c',
  ],
  c_args: [
    common_cflags,
    '-Wno-format-truncation',
    '-Wno-unused-result',
  ],
  include_directories: [
    top_inc,
    libacrn_mngr_pub_inc,
    dm_inc,
  ],
  link_with: libacrn_mngr,
  install: true,
)

executable('acrnd',
  sources: [
    'acrnd.c',
    'acrn_vm_ops.c',
  ],
  c_args: [
    common_cflags,
    '-Wno-format-truncation',
    '-Wno-unused-result',
  ],
  include_directories: [
    top_inc,
    libacrn_mngr_pub_inc,
    dm_inc,
  ],
  link_with: libacrn_mngr,
  install: true,
)
