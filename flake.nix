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
        buildInputs = [
          vulkan-headers
          vulkan-loader
          vulkan-validation-layers
          moltenvk

          vulkan-tools
          # vulkan-tools-lunarg
          vulkan-utility-libraries
          glfw
          cglm
        ];

        nativeBuildInputs = [
          pkg-config
          gnumake
        ];

        packages = [
          clang-tools
          # llvm_17
          # lldb_17
          bear
        ];

        VK_LAYER_PATH = "${vulkan-validation-layers}/share/vulkan/explicit_layer.d";
        VK_ICD_FILENAMES = "${moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
      };
    };
  }));
}
