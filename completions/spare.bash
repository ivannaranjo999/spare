_spare() {
  local cur prev words cword action i
  _init_completion || return

  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"

  # -j expects a number next; -C expects a directory
  if [[ "$prev" == "-j" ]]; then
    COMPREPLY=()
    return
  fi
  if [[ "$prev" == "-C" ]]; then
    _filedir -d
    return
  fi

  # Find which action was already typed (first non-flag positional arg)
  action=""
  for (( i = 1; i < COMP_CWORD; i++ )); do
    case "${COMP_WORDS[i]}" in
      -v|-h|-z|-S|-V) ;;
      -j) (( i++ )) ;;  # skip the number after -j
      p|pz|u|l|g|i) action="${COMP_WORDS[i]}"; break ;;
    esac
  done

  # Still on a flag
  if [[ "$cur" == -* ]]; then
    COMPREPLY=( $(compgen -W "-v -h -j -z -S -V -C" -- "$cur") )
    return
  fi

  # No action yet: complete actions and flags
  if [[ -z "$action" ]]; then
    COMPREPLY=( $(compgen -W "p pz u l g i -v -h -j -z -S -V -C" -- "$cur") )
    return
  fi

  # Action is known: figure out which positional slot we are filling
  local pos=0
  for (( i = 1; i < COMP_CWORD; i++ )); do
    case "${COMP_WORDS[i]}" in
      -v|-h|-z|-S|-V) ;;
      -j) (( i++ )) ;;
      *) (( pos++ )) ;;
    esac
  done
  # pos==1 is the action slot, pos==2 is the archive slot, pos>=3 are file slots

  if (( pos == 2 )); then
    # Archive slot: suggest existing .spare/.szt files and "-" for all actions
    local archives
    archives=( $(compgen -f -X "!*.sar" -- "$cur")
               $(compgen -f -X "!*.szt" -- "$cur") )
    [[ "$cur" == -* || "$cur" == "" ]] && archives+=( "-" )
    COMPREPLY=( "${archives[@]}" )
    compopt -o filenames 2>/dev/null
    return
  fi

  # File slots (pos >= 3): only meaningful for p, pz, g, i
  case "$action" in
    p|pz|g|i) _filedir ;;
    u|l)      COMPREPLY=() ;;
  esac
}

complete -F _spare spare
