// Tunes the Mac's Wi-Fi card to a 20 MHz channel, leaving its network first.
// With no argument, prints the current channel and changes nothing.
import CoreWLAN
import Foundation

guard let iface = CWWiFiClient.shared().interface() else {
  fputs("no Wi-Fi interface\n", stderr)
  exit(1)
}
let args = CommandLine.arguments
if args.count < 2 {
  print(iface.wlanChannel()?.channelNumber ?? 0)
  exit(0)
}
guard let number = Int(args[1]) else { exit(2) }
iface.disassociate()
guard
  let channel = iface.supportedWLANChannels()?.first(where: {
    $0.channelNumber == number && $0.channelWidth == .width20MHz
  })
else {
  fputs("channel \(number) is not supported\n", stderr)
  exit(3)
}
do {
  try iface.setWLANChannel(channel)
} catch {
  fputs("\(error)\n", stderr)
  exit(4)
}
