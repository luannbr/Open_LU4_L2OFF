-- Current Cached.exe primary opcodes 195-197 and CUser recipe-book owners.
--
-- Current machine code fixes both signed/unsigned-compatible integer input
-- positions.  All six substantive full-arity donor families have identical
-- normalized bodies and relation effects for save and delete. Incompatible
-- zero-parameter extender stubs are rejected.  lin_LoadRecipeInfo already
-- exists in migration 019.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_saverecipeinfo"(
    p_char_id integer,
    p_recipe_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_recipe" (
        "char_id",
        "recipe_id"
    ) VALUES (
        p_char_id,
        p_recipe_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delrecipeinfo"(
    p_char_id integer,
    p_recipe_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_recipe"
    WHERE "user_recipe"."char_id" = p_char_id
      AND "user_recipe"."recipe_id" = p_recipe_id;
$function$;

COMMIT;
