-- Current Cached.exe AQ 16/76 character-property persistence.
--
-- The executable fixes the ordered integer ABI for both callables. Every
-- compatible substantive donor has the same two effects: lin_ModChar2
-- updates the six user_data appearance/class columns, while
-- lin_ModSubJobClass selects subjob{N}_class from the supplied subjob ID.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_modchar2"(
    p_gender integer,
    p_race integer,
    p_class integer,
    p_face_index integer,
    p_hair_shape_index integer,
    p_hair_color_index integer,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS target
    SET "gender" = p_gender,
        "race" = p_race,
        "class" = p_class,
        "face_index" = p_face_index,
        "hair_shape_index" = p_hair_shape_index,
        "hair_color_index" = p_hair_color_index
    WHERE target."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modsubjobclass"(
    p_char_id integer,
    p_subjob_id integer,
    p_class integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    EXECUTE format(
        'UPDATE "world"."user_data" SET %I = $1 WHERE "char_id" = $2',
        'subjob' || p_subjob_id::text || '_class'
    ) USING p_class, p_char_id;
END;
$function$;

COMMIT;
