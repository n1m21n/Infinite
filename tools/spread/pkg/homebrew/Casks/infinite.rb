cask "infinite" do
  version "1.2.0"
  sha256 "613c165b748f6710a7e127a0e36bc0ade28eaf416c5d2c8146d50d5f04c080a2"

  url "https://github.com/n1m21n/Infinite/releases/download/v#{version}/Infinite.dmg",
      verified: "github.com/n1m21n/Infinite/"
  name "Infinite"
  desc "Real-time modular audiovisual workstation for visuals, music and code"
  homepage "https://infinitedevices.org/"

  livecheck do
    url :url
    strategy :github_latest
  end

  app "Infinite.app"
  binary "#{appdir}/Infinite.app/Contents/MacOS/Infinite", target: "infinite"

  # The DAW plugin (VST3 + AU) rides inside the app; Settings > DAW plugin (or
  # `infinite --install-plugins`) copies it into the user's plug-in folders.
  uninstall delete: [
    "~/Library/Audio/Plug-Ins/Components/Infinite FX.component",
    "~/Library/Audio/Plug-Ins/Components/Infinite Instrument.component",
    "~/Library/Audio/Plug-Ins/VST3/Infinite FX.vst3",
    "~/Library/Audio/Plug-Ins/VST3/Infinite Instrument.vst3",
  ]

  zap trash: [
    "~/Library/Application Support/Infinite",
    "~/Library/Preferences/org.infinitedevices.Infinite.plist",
    "~/Library/Saved Application State/org.infinitedevices.Infinite.savedState",
  ]

  caveats "DAW plugin: run `infinite --install-plugins`, or Settings > DAW plugin > Install."
end
