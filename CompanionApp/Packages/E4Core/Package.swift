// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "E4Core",
    platforms: [
        .iOS(.v17),
        .macOS(.v14)
    ],
    products: [
        .library(name: "E4Core", targets: ["E4Core"])
    ],
    targets: [
        .target(name: "E4Core"),
        .testTarget(name: "E4CoreTests", dependencies: ["E4Core"])
    ]
)
