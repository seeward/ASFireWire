import Testing
@testable import ASFW

@MainActor
struct DriverLogViewModelTests {
    private let categoryNames: [UInt8: String] = [
        5: "Async",
        17: "DirectAudio",
    ]

    private let records = [
        ASFWLogRingRecord(
            sequence: 100,
            timestampNs: 1_000,
            category: 17,
            level: 0,
            message: "[TxContent] deadline-xrun-hold-nodata deficit=8"
        ),
        ASFWLogRingRecord(
            sequence: 101,
            timestampNs: 2_000,
            category: 17,
            level: 3,
            message: "[TxPrep] exposed through frame 512"
        ),
        ASFWLogRingRecord(
            sequence: 102,
            timestampNs: 3_000,
            category: 5,
            level: 4,
            message: "[Async] request complete"
        ),
    ]

    @Test func filtersByCategoryAndMaximumLevel() {
        let result = DriverLogViewModel.matchingRecords(
            records,
            searchText: "",
            maximumLevel: 2,
            selectedCategories: [17],
            categoryNames: categoryNames
        )

        #expect(result.map(\.sequence) == [100])
    }

    // "w=480" / "payloadwriter" were substrings of the fixture message until
    // fad30cd3 replaced it; the arguments were not updated and two of the four
    // cases have been searching for text no record contains.
    @Test(arguments: ["deficit=8", "txcontent", "ERROR", "100"])
    func searchMatchesMessageCategoryLevelAndSequence(_ query: String) {
        let result = DriverLogViewModel.matchingRecords(
            records,
            searchText: query,
            maximumLevel: 4,
            selectedCategories: Set(categoryNames.keys),
            categoryNames: categoryNames
        )

        #expect(result.map(\.sequence) == [100])
    }

    @Test func searchMatchesDriverExportedCategoryName() {
        let result = DriverLogViewModel.matchingRecords(
            records,
            searchText: "directaudio",
            maximumLevel: 4,
            selectedCategories: Set(categoryNames.keys),
            categoryNames: categoryNames
        )

        #expect(result.map(\.sequence) == [100, 101])
    }

    @Test func noSelectedCategoriesProducesNoResults() {
        let result = DriverLogViewModel.matchingRecords(
            records,
            searchText: "",
            maximumLevel: 4,
            selectedCategories: [],
            categoryNames: categoryNames
        )

        #expect(result.isEmpty)
    }

    @Test func unavailableCatalogDoesNotHideRecords() {
        let result = DriverLogViewModel.matchingRecords(
            records,
            searchText: "",
            maximumLevel: 4,
            selectedCategories: [],
            categoryNames: [:]
        )

        #expect(result == records)
    }

    @Test func clipboardPayloadCopiesSelectedRowsInLogOrder() {
        let payload = DriverLogViewModel.clipboardPayload(
            records,
            selectedSequences: [102, 100],
            categoryNames: categoryNames
        )

        #expect(payload == [
            "#100 0.000001 s [DirectAudio] ERROR "
                + "[TxContent] deadline-xrun-hold-nodata deficit=8\n"
                + "#102 0.000003 s [Async] DEBUG [Async] request complete",
        ])
    }
}
