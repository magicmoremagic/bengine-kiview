demo 'kiview' {
   app {
      icon 'icon/bengine.ico',
      limp_src 'src/*.hpp',
      src 'src/*.cpp',
      define 'GLM_ENABLE_EXPERIMENTAL',
      link_project {
         'core',
         'core-id',
         'gfx',
         'platform',
         'util-fs',
         'cli',
         'util-string'
      }
   }
}
