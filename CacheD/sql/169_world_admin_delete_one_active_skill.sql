-- Current Cached.exe AQ opcode 164.
--
-- The current image fixes the exact two-integer ABI and reloads
-- lin_LoadUserActiveSkill immediately after success.  Every supplied schema
-- family implements the adjacent two-key active-skill delete against the
-- same current user_newactiveskill relation. Only the procedure spelling
-- differs.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_deleteonenewactiveskill"(
    p_char_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_newactiveskill" AS active_skill
    WHERE active_skill."char_id" = p_char_id
      AND active_skill."skill_id" = p_skill_id;
$function$;

COMMIT;
