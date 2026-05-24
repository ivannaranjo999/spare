#compdef spare

_spare() {
  local state action

  # Find the action already typed so we know what archive/file args to expect
  action=""
  local -a words_copy=("${words[@]}")
  local skip=0
  for w in "${words_copy[@]:1}"; do
    if (( skip )); then skip=0; continue; fi
    case "$w" in
      -j|-C) skip=1 ;;
      -v|-h|-z|-S|-V) ;;
      p|pz|u|l|g|i) action="$w"; break ;;
    esac
  done

  _arguments -C \
    '-v[verbose output]' \
    '-h[print help]' \
    '-V[print version]' \
    '-z[treat stdin as compressed when archive is -]' \
    '-S[detect and preserve sparse holes]' \
    '-j[use N threads (default: all cores)]:threads:( )' \
    '-C[extract files into directory]:directory:_files -/' \
    '1:action:(p pz u l g i)' \
    '2:archive:->archive' \
    '*:files:->files'

  case "$state" in
    archive)
      local -a opts
      opts=( '-:stdin/stdout' )
      opts+=( *.spa(N) *.szt(N) )
      _describe 'archive' opts
      ;;
    files)
      case "$action" in
        p|pz|g|i) _files ;;
        u|l)      ;;
      esac
      ;;
  esac
}

_spare "$@"
