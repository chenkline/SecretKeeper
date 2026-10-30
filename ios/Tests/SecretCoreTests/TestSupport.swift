import XCTest
@testable import SecretCore

/// 测试公共件：仓库根定位（向上找 test-vectors/）、JSON 读取。
enum Vectors {
    static func repoRoot() throws -> String {
        var dir = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        for _ in 0..<8 {
            let probe = dir.appendingPathComponent("test-vectors/kdf-argon2id.json")
            if FileManager.default.fileExists(atPath: probe.path) {
                return dir.path
            }
            dir = dir.deletingLastPathComponent()
        }
        throw TestFailure("test-vectors not found from \(FileManager.default.currentDirectoryPath)")
    }

    struct TestFailure: Error, CustomStringConvertible {
        let description: String
        init(_ s: String) { description = s }
    }

    static func json(_ name: String) throws -> [String: Any] {
        let path = try repoRoot() + "/test-vectors/" + name
        let data = try Data(contentsOf: URL(fileURLWithPath: path))
        let obj = try JSONSerialization.jsonObject(with: data)
        guard let dict = obj as? [String: Any] else {
            throw TestFailure("bad json: " + name)
        }
        return dict
    }

    static func vectors(_ name: String) throws -> [[String: Any]] {
        let root = try json(name)
        guard let arr = root["vectors"] as? [[String: Any]] else {
            throw TestFailure("no vectors in " + name)
        }
        return arr
    }

    static func str(_ v: [String: Any], _ k: String) -> String { v[k] as! String }
    static func int(_ v: [String: Any], _ k: String) -> Int { v[k] as! Int }
    static func bytes(_ v: [String: Any], _ k: String) -> [UInt8] {
        Crypto.unhex(str(v, k).lowercased())
    }
}

/// char-count 向量：按 Unicode 码点计。
func charCountVectors() throws -> [[String: Any]] {
    try Vectors.vectors("char-count.json")
}
