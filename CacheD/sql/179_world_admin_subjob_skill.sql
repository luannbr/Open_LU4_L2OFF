-- Current Cached.exe AQ 81/82/83/94 nonresident-subjob skill surfaces.
-- The current image fixes five integer inputs for set and three for delete.
-- Six substantive donors agree on the keyed effects, but their set procedure
-- predates the current skill-sublevel input. The already recovered
-- lin_setaquireskill function contains that exact ABI-constrained extension.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setsubjobacquireskill"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer,
    p_skill_level integer,
    p_skill_sub_level integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    SELECT "world"."lin_setaquireskill"(
        p_char_id,
        p_subjob_id,
        p_skill_id,
        p_skill_level,
        p_skill_sub_level);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletesubjobskill"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_skill"
    WHERE "char_id" = p_char_id
      AND "subjob_id" = p_subjob_id
      AND "skill_id" = p_skill_id;
$function$;

COMMIT;
