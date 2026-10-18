-- Current Cached.exe primary opcode 104 persistence.
--
-- CUser::_SetSkillLastUse binds the five int32 inputs in this order.  The
-- current machine code and all compatible five-column SQL donor families
-- agree on the update-then-insert behavior.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setskillcooltime"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer,
    p_to_end_time integer,
    p_delay integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_skill_reuse_delay"
    SET "to_end_time" = p_to_end_time,
        "skill_delay" = p_delay
    WHERE "char_id" = p_char_id
      AND "skill_id" = p_skill_id
      AND "subjob_id" = p_subjob_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_skill_reuse_delay" (
            "char_id",
            "subjob_id",
            "skill_id",
            "to_end_time",
            "skill_delay"
        ) VALUES (
            p_char_id,
            p_subjob_id,
            p_skill_id,
            p_to_end_time,
            p_delay
        );
    END IF;
END;
$function$;

COMMIT;
