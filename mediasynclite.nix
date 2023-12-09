{ pkgs ? import <nixpkgs> {} }: 


pkgs.stdenv.mkDerivation rec {
  pname = "mediasynclite";
  version = "0.4.2";

  src = pkgs.fetchgit {
    url = "https://github.com/tobz619/MediaSyncLiteLinuxNix.git";
    sha256 = "0ilm9ksvn88ky7nc3b05f1cjf4hjjc4w11wjnknhi100c8pzxqsc";
    };

  buildInputs = with pkgs; [
   gtk3
   pkg-config
   curl
   openssl.dev
   jansson
  ];

  makeFlags = [ "PREFIX=$(out)" ];

  pathsToLink = [ "share/ui/" "/bin" ];

  postPatch = ''
    substituteAllInPlace ./src/ibmsl.c
    '';
}
