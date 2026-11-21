        boards = {sheet(1, 20, 10), sheet(2, 1, 10), sheet(3, 30, 10), sheet(4, 40, 10), sheet(5, 0, 10)};
        check(multiLayoutRemovalIndices(boards, false, 1, 0, {}) == std::vector<size_t>({1}),
              "Column destruction selects the lowest required-area/cost ratio before the final board");
        size_t removalDraw = 0;
        const std::array<size_t, 3> removalDraws{3, 3, 0};
        check(multiLayoutRemovalIndices(boards, true, 1, 3, [&](size_t count) {
            check(count == 5, "Column destruction random bounds use the original layout size");
            return removalDraws.at(removalDraw++);
        }) == std::vector<size_t>({4, 3, 1, 0}), "Column destruction deduplicates all choices and removes in descending index order");
        boards[0].primaryCost = std::nextafter(.001, 0.);
        boards[1].primaryCost = .001;
        check(multiLayoutRemovalIndices(boards, false, -1, -1, {}) == std::vector<size_t>({3, 2, 1}),
              "Column destruction exact cost gate and signed request conversion");
        check(multiLayoutRemovalIndices(std::span<const MultiLnsSheet>{}, true, 99, 99, {}).empty(),
              "Empty column destruction does not call random providers");
        check(multiLayoutRemovalIndices(std::span<const MultiLnsSheet>(boards).first(1), false, 99, 0, {}).empty(),
              "The final board is excluded from ratio selection even for a single-board layout");
        check(multiLayoutRemovalIndices(std::span<const MultiLnsSheet>(boards).first(1), true, 0, 0, {}) == std::vector<size_t>({0}),
              "Explicit final-board selection remains independent of the ratio filter");
        std::string completionTrace;
        MultiColumnCompletionHost completion;
        completion.stopped = [&] { completionTrace += 'S'; return false; };
        completion.unit = [&] { completionTrace += 'U'; return .749999; };
        std::vector<std::pair<int32_t, int32_t>> completionBounds;
        int32_t completionInteger = 0;
        completion.integer = [&](int32_t first, int32_t last) {
            completionBounds.emplace_back(first, last);
            return ++completionInteger;
        };
        completion.modify = [&](bool last, int32_t low, int32_t randomCount) {
            if (!last) check(low == 2 && randomCount == 1, "Initial destruction draws random count before low-ratio count");
            else check(low == 3 && randomCount == 4, "Second destruction draws low-ratio count before random count");
            completionTrace += 'M';
        };
        completion.complete = [&] { completionTrace += 'C'; return 1; };
        const auto completed = multiCompleteColumns(completion);
        check(completed.modifiedFirst && completed.modifiedSecond && completed.addedFirst == 1 && completed.addedSecond == 1 &&
              completionTrace == "SUMCSMC" && completionBounds == std::vector<std::pair<int32_t, int32_t>>({{0, 2}, {0, 2}, {1, 3}, {1, 3}}),
              "Column destruction and both OneByOne completion passes follow source order");
        completionTrace.clear(); completionInteger = 0;
        completion.unit = [&] { completionTrace += 'U'; return .75; };
        completion.complete = [&] { completionTrace += 'C'; return 2; };
        const auto noModify = multiCompleteColumns(completion);
        check(!noModify.modifiedFirst && !noModify.modifiedSecond && completionTrace == "SUC",
              "Column destruction uses a strict .75 probability gate and short-circuits after two added boards");
        completionTrace.clear(); completion.hasTechnology = true;
        completion.complete = [&] { completionTrace += 'C'; return 0; };
        const auto technology = multiCompleteColumns(completion);
        check(!technology.modifiedFirst && !technology.modifiedSecond && completionTrace == "SUCS",
              "Configured technology disables column destruction after the original random gate consumption");
