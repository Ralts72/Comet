-- 章节内容来自项目数据；切关、角色、UI 和存档继续走引擎既有能力。
local script = {events = {
    ["tour.next"] = "next_chapter", ["tour.previous"] = "previous_chapter",
    ["tour.goto"] = "go_to", ["tour.complete"] = "complete",
    ["tour.save"] = "save", ["tour.continue"] = "continue_journey",
    ["tour.reference"] = "reference_view", ["cutscene.finished"] = "finish_intro",
}}
script.properties = {
    chapter = 1, catalog = {type = "data"}, player = {type = "entity"},
    camera = {type = "entity"}, reference_camera = {type = "entity"}, intro = false,
}
local function mark(bits, index)
    return bits:sub(1, index - 1) .. "1" .. bits:sub(index + 1)
end
function script:checkpoint_keys()
    local keys = {"tour.visited", "tour.completed"}
    for level = 1, 2 do
        for goal = 1, 3 do
            local key = string.format("character.level%d.goal%d", level, goal)
            if comet.session_get(key) ~= nil then keys[#keys + 1] = key end
        end
    end
    return keys
end
function script:on_start()
    self.ids = comet.data_ids(self.parameters.catalog)
    self.number = math.floor(self.parameters.chapter)
    self.chapter = assert(comet.data_get(self.parameters.catalog,
        string.format("%02d", self.number)), "Missing journey chapter")
    local entries = {}
    for _, id in ipairs(self.ids) do
        local row = comet.data_get(self.parameters.catalog, id)
        entries[#entries + 1] = row.section .. "\t" .. row.title
    end
    self.visited = comet.session_get("tour.visited") or string.rep("0", #self.ids)
    self.completed = comet.session_get("tour.completed") or string.rep("0", #self.ids)
    if #self.visited ~= #self.ids then self.visited = string.rep("0", #self.ids) end
    if #self.completed ~= #self.ids then self.completed = string.rep("0", #self.ids) end
    self.visited = mark(self.visited, self.number)
    comet.session_set("tour.active", true)
    comet.session_set("tour.number", self.number)
    comet.session_set("tour.total", #self.ids)
    comet.session_set("tour.section", self.chapter.section)
    comet.session_set("tour.title", self.chapter.title)
    comet.session_set("tour.needs_deferred", self.chapter.requires_deferred)
    comet.session_set("tour.collection", self.chapter.collection_challenge)
    comet.session_set("tour.description", self.chapter.description)
    comet.session_set("tour.task", self.chapter.task)
    comet.session_set("tour.entries", table.concat(entries, "\n"))
    comet.session_set("tour.visited", self.visited)
    comet.session_set("tour.completed", self.completed)
    self.has_reference = self.parameters.reference_camera
        and self.parameters.reference_camera:is_valid()
    comet.session_set("tour.has_reference", self.has_reference == true)
    self.intro = self.parameters.intro
    self:set_reference(self.intro)
    local _, start_y = self.parameters.player:position()
    self.fall_limit = math.min(-12, start_y - 25)
    self.save_pending = false
    comet.session_set("tour.save_status", "完成体验会自动保存；也可在章节菜单手动保存。")
end
function script:update()
    if comet.action_pressed("demo.restart") then
        comet.restart_scene()
        return
    end
    if self.parameters.player then
        local _, y = self.parameters.player:position()
        if y < self.fall_limit then
            comet.restart_scene()
            return
        end
    end
    if self.save_pending then
        comet.save_checkpoint("journey_start", self:checkpoint_keys())
        self.save_pending = false
        comet.session_set("tour.save_status", "已提交保存请求；继续旅程会读取最近成功保存的关卡起点。")
    end
end
function script:go_to(number)
    if type(number) ~= "number" or number % 1 ~= 0 then return end
    local row = comet.data_get(self.parameters.catalog, string.format("%02d", number))
    if not row then return end
    comet.load_scene(row.scene)
end
function script:next_chapter()
    self:go_to(self.number % #self.ids + 1)
end
function script:previous_chapter()
    self:go_to((self.number - 2) % #self.ids + 1)
end
function script:complete()
    self.completed = mark(self.completed, self.number)
    comet.session_set("tour.completed", self.completed)
    self.save_pending = true
end
function script:save()
    self.save_pending = true
end
function script:continue_journey()
    comet.load_checkpoint()
end
function script:set_reference(reference)
    reference = reference and self.has_reference == true
    comet.set_primary_camera(reference and self.parameters.reference_camera or self.parameters.camera)
    comet.session_set("tour.reference", reference)
    comet.session_set("tour.cinematic", reference)
end
function script:finish_intro()
    if not self.intro then return end
    self.intro = false
    self:set_reference(false)
end
function script:reference_view()
    if not self.has_reference then return end
    self.intro = false
    self:set_reference(not comet.session_get("tour.reference"))
end
return script
