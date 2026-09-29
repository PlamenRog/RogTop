{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    clang
    gnumake
  ];

  buildInputs = with pkgs; [
    ncurses
  ];
}
