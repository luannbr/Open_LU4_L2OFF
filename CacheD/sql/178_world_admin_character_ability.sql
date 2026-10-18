-- Current Cached.exe administrative AQ 77/78 character-ability owner:
-- CUser::ModCharExtern / ModChar3Extern and both subjob ability variants.
--
-- Current machine code fixes every input/result type and order. All
-- substantive signature-compatible donor families agree on the relational
-- effects translated below. No relation or column is introduced here.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_modchar"(
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_align integer,
    p_pk integer,
    p_pkpardon integer,
    p_duel integer,
    p_char_id integer,
    p_ap integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "sp" = p_sp,
        "exp" = p_exp,
        "lev" = p_level,
        "align" = p_align,
        "pk" = p_pk,
        "pkpardon" = p_pkpardon,
        "duel" = p_duel,
        "pp" = p_ap
    WHERE "char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modchar3"(
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_align integer,
    p_pk integer,
    p_pkpardon integer,
    p_duel integer,
    p_char_id integer,
    p_ap integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "sp" = "sp" + p_sp,
        "exp" = "exp" + p_exp,
        "lev" = p_level,
        "align" = "align" + p_align,
        "pk" = "pk" + p_pk,
        "pkpardon" = "pkpardon" + p_pkpardon,
        "duel" = "duel" + p_duel,
        "pp" = "pp" + p_ap
    WHERE "char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getexpbysubjob"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    exp bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT subjob."exp"
    FROM "world"."user_subjob" AS subjob
    WHERE subjob."char_id" = p_char_id
      AND subjob."subjob_id" = COALESCE(p_subjob_id, 0);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modsubjobability"(
    p_char_id integer,
    p_subjob_id integer,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_ap integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_subjob"
    SET "sp" = "sp" + p_sp,
        "exp" = "exp" + p_exp,
        "level" = p_level,
        "pp" = "pp" + p_ap
    WHERE "char_id" = p_char_id
      AND "subjob_id" = p_subjob_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modsubjobabilityabsolutely"(
    p_char_id integer,
    p_subjob_id integer,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_ap integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_subjob"
    SET "sp" = p_sp,
        "exp" = p_exp,
        "level" = p_level,
        "pp" = p_ap
    WHERE "char_id" = p_char_id
      AND "subjob_id" = p_subjob_id;
$function$;

COMMIT;
