/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "gtest/gtest.h"
#include "ui_driver.h"
#include "ui_model.h"

using namespace OHOS::uitest;
using namespace std;

static constexpr auto ATTR_TEXT = "text";
// record the triggered touch events.
static vector<TouchEvent> touch_event_records;

class MockController : public UiController {
public:
    explicit MockController() : UiController("mock_controller", "") {}

    ~MockController() = default;

    void SetDomFrames(vector<string> domFrames)
    {
        mockDomFrames_.clear();
        mockDomFrames_ = move(domFrames);
        frameIndex_ = 0;
    }

    void SetDomFrame(string_view domFrame)
    {
        mockDomFrames_.clear();
        mockDomFrames_.emplace_back(domFrame);
        frameIndex_ = 0;
    }

    uint32_t GetConsumedDomFrameCount() const
    {
        return frameIndex_;
    }

    void GetCurrentUiDom(nlohmann::json& out) const override
    {
        uint32_t newIndex = frameIndex_;
        frameIndex_++;
        if (newIndex >= mockDomFrames_.size()) {
            out = nlohmann::json::parse(mockDomFrames_.at(mockDomFrames_.size() - 1));
        } else {
            out = nlohmann::json::parse(mockDomFrames_.at(newIndex));
        }
    }

    void InjectTouchEventSequence(const vector<TouchEvent> &events) const override
    {
        touch_event_records = events; // copy-construct
    }

    bool IsWorkable() const override
    {
        return true;
    }

private:
    vector<string> mockDomFrames_;
    mutable uint32_t frameIndex_ = 0;
};

// test fixture
class UiDriverTest : public testing::Test {
protected:
    void SetUp() override
    {
        touch_event_records.clear();
        auto mockController = make_unique<MockController>();
        controller_ = mockController.get();
        UiController::RegisterController(move(mockController), Priority::MEDIUM);
        driver_ = make_unique<UiDriver>("");
    }

    void TearDown() override
    {
        controller_ = nullptr;
        UiController::RemoveController("mock_controller");
    }

    MockController *controller_ = nullptr;
    unique_ptr<UiDriver> driver_ = nullptr;

    ~UiDriverTest() override = default;
};

TEST_F(UiDriverTest, internalError)
{
    // give no UiController, should cause internal error
    UiController::RemoveController("mock_controller");
    auto error = ApiCallErr(NO_ERROR);
    auto image = WidgetImage();
    driver_->PerformWidgetOperate(image, WidgetOp::CLICK, error);

    ASSERT_EQ(INTERNAL_ERROR, error.code_);
}

TEST_F(UiDriverTest, normalInteraction)
{
    constexpr auto mockDom0 = R"({
"attributes": {
"index": "0",
"resource-id": "id1",
"bounds": "[0,0][100,100]",
"text": ""
},
"children": [
{
"attributes": {
"index": "0",
"resource-id": "id4",
"bounds": "[0,0][50,50]",
"text": "USB"
},
"children": []
}
]
}
)";
    controller_->SetDomFrame(mockDom0);

    auto error = ApiCallErr(NO_ERROR);
    auto selector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    selector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(selector, images, error);

    ASSERT_EQ(1, images.size());
    // perform interactions
    error = ApiCallErr(NO_ERROR);
    driver_->PerformWidgetOperate(*images.at(0), WidgetOp::CLICK, error);
    ASSERT_EQ(NO_ERROR, error.code_);
    auto key = Back();
    driver_->TriggerKey(key, error);
    ASSERT_EQ(NO_ERROR, error.code_);
    driver_->PerformWidgetOperate(*images.at(0), WidgetOp::CLICK, error);
    ASSERT_EQ(NO_ERROR, error.code_);
}

TEST_F(UiDriverTest, retrieveWidgetFailure)
{
    constexpr auto mockDom0 = R"({
"attributes": {
"text": "",
"bounds": "[0,0][100,100]"
},
"children": [
{
"attributes": {
"text": "USB",
"bounds": "[0,0][50,50]"
},
"children": []
}
]
}
)";
    constexpr auto mockDom1 = R"({
"attributes": {
"text": "",
"bounds": "[0,0][100,100]"
},
"children": [
{
"attributes": {
"text": "WYZ",
"bounds": "[0,0][50,50]"
},
"children": []
}
]
}
)";
    controller_->SetDomFrame(mockDom0);

    auto error = ApiCallErr(NO_ERROR);
    auto selector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    selector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(selector, images, error);

    ASSERT_EQ(1, images.size());

    // mock another dom on which the target widget is missing, and perform click
    controller_->SetDomFrame(mockDom1);
    error = ApiCallErr(NO_ERROR);
    driver_->PerformWidgetOperate(*images.at(0), WidgetOp::CLICK, error);

    // retrieve widget failed should be marked as exception
    ASSERT_EQ(WIDGET_LOST, error.code_);
    ASSERT_TRUE(error.message_.find(selector.Describe()) != string::npos)
                                << "Error message should contains the widget selection description";
}

TEST_F(UiDriverTest, scrollSearchRetrieveSubjectWidgetFailed)
{
    constexpr auto mockDom0 = R"({
"attributes": {
"index": "0",
"resource-id": "id1",
"bounds": "[0,0][100,100]",
"text": ""
},
"children": [
{
"attributes": {
"index": "0",
"resource-id": "id4",
"bounds": "[0,0][50,50]",
"text": "USB"
},
"children": []
}
]
})";
    constexpr auto mockDom1 = R"({"attributes":{"index":"0","resource-id":"id1","text":""},"children":[]})";
    controller_->SetDomFrame(mockDom0);

    auto error = ApiCallErr(NO_ERROR);
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);

    ASSERT_EQ(1, images.size());

    // mock another dom on which the scroll-widget is missing, and perform scroll-search
    controller_->SetDomFrame(mockDom1);
    error = ApiCallErr(NO_ERROR);
    auto targetWidgetSelector = WidgetSelector();
    ASSERT_EQ(nullptr, driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0));
    // retrieve scroll widget failed should be marked as exception
    ASSERT_EQ(WIDGET_LOST, error.code_);
    ASSERT_TRUE(error.message_.find(scrollWidgetSelector.Describe()) != string::npos)
                                << "Error message should contains the scroll-widget selection description";
}

TEST_F(UiDriverTest, scrollSearchTargetWidgetNotExist)
{
    constexpr auto mockDom = R"({
"attributes": {
"index": "0",
"resource-id": "id1",
"bounds": "[0,0][100,100]",
"text": ""
},
"children": [
{
"attributes": {
"index": "0",
"resource-id": "id4",
"bounds": "[0,0][50,50]",
"text": "USB"
},
"children": []
}
]
}
)";
    controller_->SetDomFrame(mockDom);

    auto error = ApiCallErr(NO_ERROR);
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);

    ASSERT_EQ(1, images.size());

    error = ApiCallErr(NO_ERROR);
    auto targetWidgetMatcher = WidgetAttrMatcher(ATTR_TEXT, "wyz", EQ);
    auto targetWidgetSelector = WidgetSelector();
    targetWidgetSelector.AddMatcher(targetWidgetMatcher);
    ASSERT_EQ(nullptr, driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0));
}

TEST_F(UiDriverTest, scrollSearchCheckSubjectWidget)
{
    constexpr auto mockDom = R"({
"attributes": {
"bounds": "[0,0][1200,2000]",
"text": ""
},
"children": [
{
"attributes": {
"bounds": "[0,200][600,1000]",
"text": "USB"
},
"children": []
}
]
}
)";
    controller_->SetDomFrame(mockDom);

    auto error = ApiCallErr(NO_ERROR);
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);

    ASSERT_EQ(1, images.size());

    error = ApiCallErr(NO_ERROR);
    auto targetWidgetMatcher = WidgetAttrMatcher(ATTR_TEXT, "wyz", EQ);
    auto targetWidgetSelector = WidgetSelector();
    targetWidgetSelector.AddMatcher(targetWidgetMatcher);
    driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0);
    // check the scroll action events, should be acted on the subject node specified by WidgetMatcher
    ASSERT_TRUE(!touch_event_records.empty());
    auto &firstEvent = touch_event_records.at(0);
    auto &lastEvent = touch_event_records.at(touch_event_records.size() - 1);
    // check scroll event pointer_x
    int32_t subjectCx = (0 + 600) / 2;
    ASSERT_NEAR(firstEvent.point_.px_, subjectCx, 5);
    ASSERT_NEAR(lastEvent.point_.px_, subjectCx, 5);

    // check scroll event pointer_y
    constexpr int32_t subjectWidgetHeight = 1000 - 200;
    int32_t maxCy = 0;
    int32_t minCy = 1E5;
    for (auto &event:touch_event_records) {
        if (event.point_.py_ > maxCy) {
            maxCy = event.point_.py_;
        }
        if (event.point_.py_ < minCy) {
            minCy = event.point_.py_;
        }
    }

    int32_t scrollDistanceY = maxCy - minCy;
    ASSERT_TRUE(abs(scrollDistanceY - subjectWidgetHeight) < 5);
}

TEST_F(UiDriverTest, scrollSearchCheckDirection)
{
    constexpr auto mockDom = R"({
"attributes": {
"bounds": "[0,0][100,100]",
"text": ""
},
"children": [
{
"attributes": {
"bounds": "[0,0][50,50]",
"text": "USB"
},
"children": []
}]})";
    controller_->SetDomFrame(mockDom);

    auto error = ApiCallErr(NO_ERROR);
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);
    ASSERT_EQ(1, images.size());

    error = ApiCallErr(NO_ERROR);
    auto targetWidgetMatcher = WidgetAttrMatcher(ATTR_TEXT, "wyz", EQ);
    auto targetWidgetSelector = WidgetSelector();
    targetWidgetSelector.AddMatcher(targetWidgetMatcher);
    driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0);
    // check the scroll action events, should be acted on the specified node
    ASSERT_TRUE(!touch_event_records.empty());
    // should scroll-search upward (cy_from<cy_to) then downward (cy_from>cy_to)
    int32_t maxCyEventIndex = 0;
    uint32_t index = 0;
    for (auto &event:touch_event_records) {
        if (event.point_.py_ > touch_event_records.at(maxCyEventIndex).point_.py_) {
            maxCyEventIndex = index;
        }
        index++;
    }

    for (size_t idx = 0; idx < touch_event_records.size() - 1; idx++) {
        if (idx < maxCyEventIndex) {
            ASSERT_LT(touch_event_records.at(idx).point_.py_, touch_event_records.at(idx + 1).point_.py_);
        } else if (idx > maxCyEventIndex) {
            ASSERT_GT(touch_event_records.at(idx).point_.py_, touch_event_records.at(idx + 1).point_.py_);
        }
    }
}

/**
 * The expected scroll-search count. The search starts upward till the target widget
 * is found or reach the top (DOM snapshot becomes frozen); then search downward till
 * the target widget is found or reach the bottom (DOM snapshot becomes frozen); */
TEST_F(UiDriverTest, scrollSearchCheckCount_targetNotExist)
{
    auto error = ApiCallErr(NO_ERROR);
    // mocked widget text
    const vector<string> domFrameSet[4] = {
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WLJ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WLJ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WLJ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        }
    };

    controller_->SetDomFrames(domFrameSet[0]); // set frame, let the scroll-widget be found firstly
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);

    ASSERT_EQ(1, images.size());

    auto targetWidgetMatcher = WidgetAttrMatcher(ATTR_TEXT, "xyz", EQ); // widget that will never be found
    auto targetWidgetSelector = WidgetSelector();
    targetWidgetSelector.AddMatcher(targetWidgetMatcher);

    const uint32_t expectedSearchCount[] = {3, 4, 5, 5};
    vector<string> domFrames;
    for (size_t index = 0; index < 4; index++) {
        controller_->SetDomFrames(domFrameSet[index]);
        // check search result
        ASSERT_EQ(nullptr, driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0));
        // check scroll-search count
        ASSERT_EQ(expectedSearchCount[index], controller_->GetConsumedDomFrameCount()) << index;
    }
}

TEST_F(UiDriverTest, scrollSearchCheckCount_targetExist)
{
    auto error = ApiCallErr(NO_ERROR);
    const vector<string> domFrameSet[4] = {
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WLJ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"XYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        },
        {
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"USB"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"XYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WLJ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})",
            R"({"attributes":{"bounds":"[0,0][100,100]","hashcode":"123","id":"100","text":"WYZ"},"children":[]})"
        }
    };

    controller_->SetDomFrames(domFrameSet[1]); // set frame, let the scroll-widget be found firstly
    auto scrollWidgetSelector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    scrollWidgetSelector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(scrollWidgetSelector, images, error);
    ASSERT_EQ(1, images.size());

    auto targetWidgetMatcher = WidgetAttrMatcher(ATTR_TEXT, "WYZ", EQ);
    auto targetWidgetSelector = WidgetSelector();
    targetWidgetSelector.AddMatcher(targetWidgetMatcher);

    const uint32_t expectedSearchCount[] = {1, 2, 3, 4};
    vector<string> domFrames;
    for (size_t index = 0; index < 4; index++) {
        controller_->SetDomFrames(domFrameSet[index]);
        // check search result
        ASSERT_NE(nullptr, driver_->ScrollSearch(*images.at(0), targetWidgetSelector, error, 0));
        // check scroll-search count
        ASSERT_EQ(expectedSearchCount[index], controller_->GetConsumedDomFrameCount()) << index;
    }
}

TEST_F(UiDriverTest, widget2Image)
{
    constexpr auto mockDom = R"({
"attributes": {
"bounds": "[0,0][100,100]",
"index": "0",
"resource-id": "id1",
"text": ""
},
"children": [
{
"attributes": {
"bounds": "[0,0][100,100]",
"hashcode": "888",
"index": "0",
"resource-id": "id4",
"text": "USB"
},
"children": []
}
]
}
)";
    controller_->SetDomFrame(mockDom);

    auto error = ApiCallErr(NO_ERROR);
    auto selector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_TEXT, "USB", EQ);
    selector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(selector, images, error);

    ASSERT_EQ(1, images.size());
    // check attributes are correctly inflated
    ASSERT_EQ("888", images.at(0)->GetHashCode());
    ASSERT_TRUE(images.at(0)->GetSelectionDesc().find(selector.Describe()) != string::npos);
}

TEST_F(UiDriverTest, updateWidgetImage)
{
    constexpr auto mockDom0 = R"({
"attributes": {
"bounds": "[0,0][100,100]",
"text": ""},
"children": [
{
"attributes": {
"bounds": "[0,0][50,50]",
"hashcode": "12345",
"text": "USB"},
"children": []}]})";
    controller_->SetDomFrame(mockDom0);

    auto error = ApiCallErr(NO_ERROR);
    auto selector = WidgetSelector();
    auto matcher = WidgetAttrMatcher(ATTR_HASHCODE, "12345", EQ);
    selector.AddMatcher(matcher);
    vector<unique_ptr<WidgetImage>> images;
    driver_->FindWidgets(selector, images, error);
    ASSERT_EQ(1, images.size());
    ASSERT_EQ("USB", images.at(0)->GetAttribute(ATTR_TEXT, ""));

    // mock new UI
    constexpr auto mockDom1 = R"({
"attributes": {
"bounds": "[0,0][100,100]",
"text": ""},
"children": [
{
"attributes": {
"bounds": "[0,0][50,50]",
"hashcode": "12345",
"text": "WYZ"},
"children": []}]})";
    controller_->SetDomFrame(mockDom1);
    // we should be able to refresh WidgetImage on the new UI
    driver_->UpdateWidgetImage(*images.at(0), error);
    ASSERT_EQ(NO_ERROR, error.code_);
    ASSERT_EQ("WYZ", images.at(0)->GetAttribute(ATTR_TEXT, "")); // attribute should be updated to new value

    // mock new UI
    constexpr auto mockDom2 = R"({
"attributes": {
"bounds": "[0,0][100,100]",
"text": ""},
"children": [
{
"attributes": {
"bounds": "[0,0][50,50]",
"hashcode": "23456",
"text": "ZL"},
"children": []}]})";
    controller_->SetDomFrame(mockDom2);
    // we should not be able to refresh WidgetImage on the new UI since its gone (hashcode and attributes changed)
    driver_->UpdateWidgetImage(*images.at(0), error);
    ASSERT_EQ(WIDGET_LOST, error.code_);
}