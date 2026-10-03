local score = {}

score.changed_event = "demo.score_changed"

function score.get()
    return comet.session_get("demo.score") or 0
end

function score.add(amount)
    local value = score.get() + amount
    comet.session_set("demo.score", value)
    comet.emit(score.changed_event, value)
    return value
end

return score
