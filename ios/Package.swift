// swift-tools-version:5.9
// 密匣 iOS 核心层（铁律二：移动端原生实现；本包只含平台无关的四层逻辑，
// SwiftUI 界面随后续阶段加在 Xcode 工程里，不进本包）。
import PackageDescription

let package = Package(
    name: "SecretKeeperCore",
    products: [
        .library(name: "SecretCore", targets: ["SecretCore"])
    ],
    targets: [
        // SQLite3 系统库 shim：Linux 需要 libsqlite3-dev，macOS/iOS 用系统 SDK。
        .systemLibrary(
            name: "CSQLite",
            path: "CSQLite"
        ),
        .target(
            name: "SecretCore",
            dependencies: ["CSQLite"],
            path: "Core"
        ),
        .testTarget(
            name: "SecretCoreTests",
            dependencies: ["SecretCore"],
            path: "Tests/SecretCoreTests"
        )
    ]
)
