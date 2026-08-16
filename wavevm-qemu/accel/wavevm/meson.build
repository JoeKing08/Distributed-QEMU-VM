wavevm_ss = ss.source_set()
wavevm_ss.add(files(
  'wavevm-all.c',
  'wavevm-cpu.c',
  'wavevm-tcg.c',
  'wavevm-user-mem.c',
  '../../hw/wavevm/wavevm-mem.c',
  '../../../common_include/wavevm_sha256.c',
  '../../../common_include/wavevm_envelope_v1.c',
  '../../../common_include/wavevm_memory_v1.c',
  '../../../common_include/wavevm_local_memory_v1.c',
))

specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: wavevm_ss)
