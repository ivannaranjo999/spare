# Helper: true if an action has already been typed
function __spare_has_action
  set -l tokens (commandline -opc)
  for t in $tokens[2..]
    switch $t
      case p pz u l g i
        return 0
    end
  end
  return 1
end

# Helper: return the action that was typed
function __spare_action
  set -l tokens (commandline -opc)
  for t in $tokens[2..]
    switch $t
      case p pz u l g i
        echo $t
        return
    end
  end
end

# Actions (only when no action typed yet)
complete -c spare -f -n 'not __spare_has_action' -a p   -d 'Pack files to a SAR archive'
complete -c spare -f -n 'not __spare_has_action' -a pz  -d 'Pack and compress (zstd)'
complete -c spare -f -n 'not __spare_has_action' -a u   -d 'Unpack SAR archive'
complete -c spare -f -n 'not __spare_has_action' -a l   -d 'List archive contents'
complete -c spare -f -n 'not __spare_has_action' -a g   -d 'Grab specific files from archive'
complete -c spare -f -n 'not __spare_has_action' -a i   -d 'Insert files into archive'

# Flags (always available)
complete -c spare -f -s v -d 'Verbose output'
complete -c spare -f -s h -d 'Print help'
complete -c spare -f -s V -d 'Print version'
complete -c spare -f -s z -d 'Treat stdin as compressed when archive is -'
complete -c spare -f -s S -d 'Detect and preserve sparse holes'
complete -c spare -f -s j -d 'Number of threads (default: all cores)'
complete -c spare    -s C -d 'Extract files into directory' -r -F

# Archive file completion (after action is known)
complete -c spare -n '__spare_has_action' -a '(ls 2>/dev/null | string match -r ".*\.(spa|szt)"; echo -)' -d 'Archive'

# File completion for actions that take file arguments
complete -c spare -n '__spare_has_action; and string match -qr "^(p|pz|g|i)\$" (__spare_action)' -F
