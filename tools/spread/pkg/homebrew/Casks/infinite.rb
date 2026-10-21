cask "infinite" do
  version "0.6.0"
  sha256 "745f2a24fe3eb6340fdf8bd3eb7ba892d4be2205358366b72fd2d74db0a16aa9"

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

  zap trash: [
    "~/Library/Application Support/Infinite",
    "~/Library/Preferences/org.infinitedevices.Infinite.plist",
    "~/Library/Saved Application State/org.infinitedevices.Infinite.savedState",
  ]
end
