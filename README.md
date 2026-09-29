# RogTop
`RogTop` is a Linux system-monitoring TUI written in C using ncurses. It provides live system visualizers, a sortable process list, fuzzy live search, and process signal actions.

## Dependencies
 - Clang
 - ncurses
 - GNU Make

## Build
Dependencies are listed in nix shell file. You can use it with: 

```sh
nix-shell --run 'make'
```

or download them seperately and simply do

```sh
make
```

## Note
While you can techinically compile this project on windows, it is only functional on Linux, FHS compliant systems as it reads from `/proc` and `/sys`.
