-- CharacterAnimStateMachine.lua
-- speed(), isJumping(), isGrounded() 는 C++에서 BindProperty로 주입된 함수

------------------------------------------------------------------------
-- 상태 정의
------------------------------------------------------------------------
local STATES = {
    Idle  = "Idle",
    Walk  = "Walk",
    Jump  = "Jump",
    Land  = "Land",
}

local STATE_SEQUENCE = {
    [STATES.Idle] = "Asset/Mesh/Manny/Anims/MM_Idle_AnimSequence.uasset",
    [STATES.Walk] = "Asset/Mesh/Manny/Anims/MF_Unarmed_Walk_Fwd_AnimSequence.uasset",
    [STATES.Jump] = "Asset/Mesh/Manny/Anims/MM_Jump_AnimSequence.uasset",
    [STATES.Land] = "Asset/Mesh/Manny/Anims/MM_Land_AnimSequence.uasset",
}

------------------------------------------------------------------------
-- 전환 규칙
------------------------------------------------------------------------
local TRANSITIONS = {
    { from = STATES.Idle, to = STATES.Walk, condition = function() return speed() > 0.5              end },
    { from = STATES.Walk, to = STATES.Idle, condition = function() return speed() <= 0.5             end },
    { from = STATES.Idle, to = STATES.Jump, condition = function() return isJumping()                end },
    { from = STATES.Walk, to = STATES.Jump, condition = function() return isJumping()                end },
    { from = STATES.Jump, to = STATES.Land, condition = function() return isGrounded() and not isJumping() end },
    { from = STATES.Land, to = STATES.Idle, condition = function() return true                       end },
}

------------------------------------------------------------------------
-- 콜백
------------------------------------------------------------------------
function onInit()
    print("[AnimSM] onInit — initial state: " .. self:getCurrentState())
    self:transitionTo(STATES.Idle)
end

function onUpdate(dt)
    local cur = self:getCurrentState()
    for _, rule in ipairs(TRANSITIONS) do
        if rule.from == cur and rule.condition() then
            self.blendDuration = 0.15
            self:transitionTo(rule.to)
            break
        end
    end
end

function onTransition(newState)
    local seqName = STATE_SEQUENCE[newState]
    if seqName then
        self:setSequence(seqName)
    end
    print("[AnimSM] -> " .. newState)
end
