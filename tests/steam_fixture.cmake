# Fresh fake Steam folder for the Add to Steam script tests: one local user,
# no shortcuts.vdf yet. Never the real Steam folder.
file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}/userdata/4242/config")
