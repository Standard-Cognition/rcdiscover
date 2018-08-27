with import <nixpkgs> {};
stdenv.mkDerivation rec {
  name = "rcdiscover";
  env = pkgs.buildEnv { name = name; paths = propagatedBuildInputs; };
  builder = builtins.toFile "builder.sh" ''
    source $stdenv/setup; ln -s $env $out
  '';

  buildInputs = [ cmake ];
  propagatedBuildInputs = [ glib libusb ];
  LDFLAGS="-pthread -lpthread";

}
