-- Current Cached.exe extended slots 411-420: pledge sign-in, faction points,
-- Balthus event tokens, per-inzone points and the all-user World Olympiad
-- stream. Wedding load reuses migration 156 and friend detail reads existing
-- CacheD owners, so neither introduces another relation here.
--
-- Current machine code fixes every function name and parameter/result order.
-- TotalChecker2015.pdb additionally keeps Faction::SET=0,
-- Faction::MODIFY=1 and all packet value layouts. No supplied database image
-- keeps these five exact procedure bodies. Direct parsing of the populated
-- World52/World103 report MDFs does keep the exact user_faction physical
-- shape and key also present in modified-287. The faction SET/MODIFY algorithm
-- remains current-ABI-constrained: SET replaces a value, MODIFY adds to it,
-- and the explicit maximum argument caps the stored result. Balthus/in-zone
-- storage remains portable. No unobserved history or gameplay
-- state is introduced.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_signinpledge"(
    p_pledge_id integer,
    p_pledge_type integer,
    p_member_id integer
) RETURNS TABLE (result integer)
LANGUAGE sql
VOLATILE
AS $function$
    SELECT joined."result"
    FROM "world"."lin_joinpledgemember"(
        p_pledge_id,
        p_pledge_type,
        p_member_id
    ) AS joined
$function$;

CREATE TABLE IF NOT EXISTS "world"."user_faction" (
    "char_id" integer NOT NULL,
    "faction_id" integer NOT NULL,
    "faction_point" bigint NOT NULL,
    CONSTRAINT "pk_user_faction"
        PRIMARY KEY ("char_id", "faction_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_loadfactionpoint"(
    p_character_id integer
) RETURNS TABLE (
    faction_type integer,
    point bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT faction."faction_id",
           faction."faction_point"
    FROM "world"."user_faction" AS faction
    WHERE faction."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatefactionpoint"(
    p_command integer,
    p_character_id integer,
    p_faction_type integer,
    p_point bigint,
    p_maximum_point bigint
) RETURNS TABLE (point bigint)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_point bigint;
BEGIN
    INSERT INTO "world"."user_faction" (
        "char_id",
        "faction_id",
        "faction_point"
    ) VALUES (
        p_character_id,
        p_faction_type,
        LEAST(p_point, p_maximum_point)
    )
    ON CONFLICT ("char_id", "faction_id") DO UPDATE
    SET "faction_point" = LEAST(
        CASE p_command
            WHEN 0 THEN p_point
            WHEN 1 THEN "world"."user_faction"."faction_point" + p_point
            ELSE "world"."user_faction"."faction_point"
        END,
        p_maximum_point
    )
    RETURNING "user_faction"."faction_point"
    INTO v_point;

    RETURN QUERY SELECT v_point;
END;
$function$;

CREATE TABLE IF NOT EXISTS "world"."balthus_event_user_info" (
    "char_id" integer NOT NULL,
    "remain_token_count" integer NOT NULL,
    CONSTRAINT "pk_balthus_event_user_info" PRIMARY KEY ("char_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_loadbalthuseventuserinfo"(
    p_character_id integer
) RETURNS TABLE (remain_token_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(
        (
            SELECT info."remain_token_count"
            FROM "world"."balthus_event_user_info" AS info
            WHERE info."char_id" = p_character_id
        ),
        -1
    )
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatebalthuseventuserinfo"(
    p_character_id integer,
    p_remain_token_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."balthus_event_user_info" (
        "char_id",
        "remain_token_count"
    ) VALUES (
        p_character_id,
        p_remain_token_count
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "remain_token_count" = EXCLUDED."remain_token_count"
$function$;

CREATE TABLE IF NOT EXISTS "world"."user_inzone_point" (
    "char_id" integer NOT NULL,
    "inzone_type_id" integer NOT NULL,
    "point" integer NOT NULL,
    "update_time" integer NOT NULL,
    CONSTRAINT "pk_user_inzone_point"
        PRIMARY KEY ("char_id", "inzone_type_id")
);

-- The current four-int DB adapter order is type, character, time, point. It
-- differs from RQ_UPDATE_USER_INZONE_POINT's field order.
CREATE OR REPLACE FUNCTION "world"."lin_updateuserinzonepoint"(
    p_inzone_type_id integer,
    p_character_id integer,
    p_update_time integer,
    p_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_inzone_point" (
        "char_id",
        "inzone_type_id",
        "point",
        "update_time"
    ) VALUES (
        p_character_id,
        p_inzone_type_id,
        p_point,
        p_update_time
    )
    ON CONFLICT ("char_id", "inzone_type_id") DO UPDATE
    SET "point" = EXCLUDED."point",
        "update_time" = EXCLUDED."update_time"
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserinzonepoint"(
    p_character_id integer
) RETURNS TABLE (
    inzone_type_id integer,
    point integer,
    update_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT inzone."inzone_type_id",
           inzone."point",
           inzone."update_time"
    FROM "world"."user_inzone_point" AS inzone
    WHERE inzone."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_wolympiadloadalluser"()
RETURNS TABLE (
    world_id integer,
    user_db_id integer,
    hero_type integer,
    win_count integer,
    previous_point integer,
    olympiad_point integer,
    match_count integer,
    olympiad_win_count integer,
    olympiad_lose_count integer,
    olympiad_trade_point integer,
    match_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT participant."world_id",
           participant."user_db_id",
           participant."hero_type",
           participant."win_count",
           participant."previous_point",
           participant."olympiad_point",
           participant."match_count",
           participant."olympiad_win_count",
           participant."olympiad_lose_count",
           participant."trade_point",
           COALESCE(matching."point", 0)::integer
    FROM "world"."world_olympiad_user" AS participant
    LEFT JOIN "world"."world_olympiad_matching_point" AS matching
      ON matching."world_id" = participant."world_id"
     AND matching."user_db_id" = participant."user_db_id"
$function$;

COMMIT;
