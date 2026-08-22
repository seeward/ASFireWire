import Foundation

struct DuetFaderScaleMark: Identifiable {
    let label: String
    let position: Double

    var id: String { label }
}
