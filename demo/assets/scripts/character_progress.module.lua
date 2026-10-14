-- 两关的收集记录属于项目规则；只保存显式进度，继续时从该关起点出发。
local progress = {}
local keys = {}
for level = 1, 2 do
    for goal = 1, 3 do
        keys[#keys + 1] = "character.level" .. level .. ".goal" .. goal
    end
end

local function key(level, goal)
    return string.format("character.level%.0f.goal%.0f", level, goal)
end

function progress.initialize()
    for _, name in ipairs(keys) do
        if comet.session_get(name) == nil then comet.session_set(name, false) end
    end
end

function progress.collected(level, goal)
    return comet.session_get(key(level, goal)) == true
end

function progress.score(level)
    local score = 0
    for goal = 1, 3 do
        if progress.collected(level, goal) then score = score + 1 end
    end
    return score
end

function progress.sync(level)
    local score = progress.score(level)
    comet.session_set("character.level", level)
    comet.session_set("character.score", score)
    comet.session_set("character.finished", score == 3)
end

function progress.save()
    comet.save_checkpoint("course_start", keys)
end

function progress.collect(level, goal)
    comet.session_set(key(level, goal), true)
    progress.sync(level)
    comet.session_set("character.save_pending", true)
end

return progress
