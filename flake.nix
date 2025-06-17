{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }: (flake-utils.lib.eachDefaultSystem (system: let
    pkgs = import nixpkgs {
      inherit system;
      config = {
        # https://github.com/NixOS/nixpkgs/issues/342876
        allowUnsupportedSystem = true;
      };
    };
    # pkgs = nixpkgs.legacyPackages.${system};
  in {
    devShells = with pkgs; {
      default = mkShell {
        buildInputs = let
          glfw-vulkan-macos-fix = glfw.overrideAttrs (oldAttrs: {
            env = {
              NIX_CFLAGS_COMPILE = toString [
                "-D_GLFW_VULKAN_LIBRARY=\"${lib.getLib vulkan-loader}/lib/libvulkan.1.dylib\""
              ];
            };
          });
        in
          [
            vulkan-headers
            vulkan-loader
            vulkan-validation-layers
            vulkan-tools
            vulkan-utility-libraries
            cglm
            shaderc
          ]
          ++ (
            lib.optionals (pkgs.stdenv.hostPlatform.isDarwin) [
              moltenvk
              glfw-vulkan-macos-fix
            ]
          )
          ++ (
            lib.optionals (pkgs.stdenv.hostPlatform.isLinux) [
              glfw
            ]
          );

        nativeBuildInputs = [
          pkg-config
          gnumake
        ];

        packages = [
          clang-tools
          clang
          # llvm_17
          # lldb_17
          bear
        ];

        VK_LAYER_PATH = "${vulkan-validation-layers}/share/vulkan/explicit_layer.d";
        VK_ICD_FILENAMES =
          if pkgs.stdenv.hostPlatform.isDarwin
          then "${moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json"
          else null;
      };
    };
  }));
}
