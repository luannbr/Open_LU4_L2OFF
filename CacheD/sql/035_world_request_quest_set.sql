-- Current Cached.exe primary opcode 23 (RequestQuestSet) quest-memo
-- persistence.
--
-- CUser::SaveMemo 0x005a8ad4 and its exact binding adapter 0x005a6e60
-- establish the eight-input lin_UpdateUserQuest ABI (seven bound int32
-- values plus literal type 0) and the two-input lin_DeleteUserQuest ABI.
-- Four substantive full-arity-compatible donor families have identical
-- normalized bodies and relational effects for both procedures.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updateuserquest"(
    p_char_id integer,
    p_quest_no integer,
    p_journal integer,
    p_state1 integer,
    p_state2 integer,
    p_state3 integer,
    p_state4 integer,
    p_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_quest" (
        "char_id",
        "quest_no",
        "journal",
        "state1",
        "state2",
        "state3",
        "state4",
        "type"
    ) VALUES (
        p_char_id,
        p_quest_no,
        p_journal,
        p_state1,
        p_state2,
        p_state3,
        p_state4,
        p_type
    )
    ON CONFLICT ("char_id", "quest_no") DO UPDATE
    SET "journal" = EXCLUDED."journal",
        "state1" = EXCLUDED."state1",
        "state2" = EXCLUDED."state2",
        "state3" = EXCLUDED."state3",
        "state4" = EXCLUDED."state4";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteuserquest"(
    p_char_id integer,
    p_quest_no integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- Donor lin_DeleteUserQuest calls lin_DeleteUserNPCLog first. Keep the
    -- same statement order without adding an unused portable helper ABI.
    DELETE FROM "world"."user_npc_log"
    WHERE "char_id" = p_char_id
      AND "quest_no" = p_quest_no;

    DELETE FROM "world"."user_quest"
    WHERE "char_id" = p_char_id
      AND "quest_no" = p_quest_no;
END;
$function$;

COMMIT;
