{ pkgs ? import <nixpkgs> {} }: 


pkgs.stdenv.mkDerivation rec {
  pname = "mediasynclite";
  version = "0.4.2";

  src = pkgs.fetchgit {
    url = "https://github.com/iBroadcastMediaServices/MediaSyncLiteLinux.git";
    rev = "f215b082e7c5a2619503b073c27b9eda91a04275";
    sha256 = "1c5iglf00askqv65x40lipzkkrjxvwcm6ip1wsbgccd14qk74553";
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

  postInstall = ''
    substituteAllInPlace ./src/ibmsl.c
    ''
}
