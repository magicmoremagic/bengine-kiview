demo 'kiview' {
   app {
      icon 'icon/bengine.ico',
      limp_src 'src/*.hpp',
      src 'src/*.cpp',
      link_project {
         'core',
         'core-id'
      }
   }
}
