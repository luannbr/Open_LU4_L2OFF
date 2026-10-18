-- Current Cached.exe Shared/DXPacket.cpp DX slots 53 through 65.
--
-- Classic 3.0, modified 287 and Ertheia keep identical substantive bodies
-- for the five Instant Agit/Olympiad/matching-point procedures below. No
-- available database source keeps the later *DecoNpc procedures or their
-- physical instant-agit table name. Their exact current input/output ABIs,
-- success value zero and key fields are fixed by Cached.exe and the paired
-- L2Server.exe. The named portable table extends the already
-- documented residence carrier from migration 038 without claiming an
-- original deployment-table identifier.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addinstantagitdeco"(
    p_instant_agit_id integer,
    p_pledge_id integer,
    p_added_deco_id integer,
    p_removed_deco_id integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    UPDATE "world"."instant_agit_deco" AS deco
    SET "deco_id" = p_added_deco_id
    WHERE deco."instant_agit_id" = p_instant_agit_id
      AND deco."pledge_id" = p_pledge_id
      AND deco."deco_id" = p_removed_deco_id;
    GET DIAGNOSTICS v_rows = ROW_COUNT;

    IF v_rows = 0 THEN
        INSERT INTO "world"."instant_agit_deco" (
            "instant_agit_id",
            "pledge_id",
            "deco_id"
        ) VALUES (
            p_instant_agit_id,
            p_pledge_id,
            p_added_deco_id
        );
        GET DIAGNOSTICS v_rows = ROW_COUNT;
    END IF;
    RETURN QUERY SELECT v_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_applyinstantagit"(
    p_pledge_id integer,
    p_instant_agit_id integer,
    p_apply_fee integer,
    p_apply_date integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    INSERT INTO "world"."instant_agit_apply" (
        "pledge_id",
        "apply_instant_agit_id"
    ) VALUES (
        p_pledge_id,
        p_instant_agit_id
    );
    GET DIAGNOSTICS v_rows = ROW_COUNT;

    IF v_rows > 0 THEN
        INSERT INTO "world"."instant_agit_history" (
            "pledge_id",
            "instant_agit_id",
            "apply_date",
            "apply_fee"
        ) VALUES (
            p_pledge_id,
            p_instant_agit_id,
            p_apply_date,
            p_apply_fee
        );
    END IF;
    RETURN QUERY SELECT v_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_returninstantagit"(
    p_pledge_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."instant_agit_deco" AS deco
    WHERE deco."pledge_id" = p_pledge_id;

    DELETE FROM "world"."instant_agit" AS agit
    WHERE agit."pledge_id" = p_pledge_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetolympiadpointbyid"(
    p_char_id integer,
    p_reset_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_nobless" AS nobless
    SET "olympiad_point" = p_reset_point
    WHERE nobless."char_id" = p_char_id;

    IF p_reset_point < 0 THEN
        UPDATE "world"."user_nobless" AS nobless
        SET "olympiad_point" = 0
        WHERE nobless."char_id" = p_char_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savematchingpoint"(
    p_char_id integer,
    p_pledgegame_point bigint,
    p_olympiad_point bigint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_matching_point" AS point
    SET "pledgegame_point" = p_pledgegame_point,
        "olympiad_point" = p_olympiad_point
    WHERE point."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_matching_point" (
            "char_id",
            "pledgegame_point",
            "olympiad_point"
        ) VALUES (
            p_char_id,
            p_pledgegame_point,
            p_olympiad_point
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_agitdeconpcset"(
    p_residence_id integer,
    p_slot integer,
    p_deco_npc_id integer,
    p_expire_time integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."cached_portable_residence_deco_npc" (
        "residence_id",
        "slot",
        "deco_npc_id",
        "expire_time"
    ) VALUES (
        p_residence_id,
        p_slot,
        p_deco_npc_id,
        p_expire_time
    )
    ON CONFLICT ("residence_id", "slot") DO UPDATE
    SET "deco_npc_id" = EXCLUDED."deco_npc_id",
        "expire_time" = EXCLUDED."expire_time";
    RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_agitdeconpcreset"(
    p_residence_id integer,
    p_slot integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."cached_portable_residence_deco_npc" AS npc
    WHERE npc."residence_id" = p_residence_id
      AND npc."slot" = p_slot;
    RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_agitdeconpcresetall"(
    p_residence_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_residence_deco_npc" AS npc
    WHERE npc."residence_id" = p_residence_id;
$function$;

CREATE TABLE "world"."cached_portable_instant_agit_deco_npc" (
    "instant_agit_id" integer NOT NULL,
    "pledge_id" integer NOT NULL,
    "slot" integer NOT NULL,
    "deco_npc_id" integer NOT NULL,
    "expire_time" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_instant_agit_deco_npc"
        PRIMARY KEY ("instant_agit_id", "pledge_id", "slot")
);

COMMENT ON TABLE "world"."cached_portable_instant_agit_deco_npc" IS
    'Portable storage behind current InstantAgitDecoNpc set/reset ABIs; the original physical table name is unavailable.';

CREATE OR REPLACE FUNCTION "world"."lin_instantagitdeconpcloadall"()
RETURNS TABLE (
    "instant_agit_id" integer,
    "slot" integer,
    "deco_npc_id" integer,
    "expire_time" integer
)
LANGUAGE sql
STABLE
AS $function$
    -- Current InstantAgitJobHandler::LoadInstantAgitDecoNpc 0x0053e1c4
    -- groups column one by instant-agit ID and assembles columns two through
    -- four as (slot, deco NPC ID, expire time).
    SELECT
        npc."instant_agit_id",
        npc."slot",
        npc."deco_npc_id",
        npc."expire_time"
    FROM "world"."cached_portable_instant_agit_deco_npc" AS npc
    ORDER BY npc."instant_agit_id", npc."slot";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_instantagitdeconpcset"(
    p_instant_agit_id integer,
    p_pledge_id integer,
    p_slot integer,
    p_deco_npc_id integer,
    p_expire_time integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."cached_portable_instant_agit_deco_npc" (
        "instant_agit_id",
        "pledge_id",
        "slot",
        "deco_npc_id",
        "expire_time"
    ) VALUES (
        p_instant_agit_id,
        p_pledge_id,
        p_slot,
        p_deco_npc_id,
        p_expire_time
    )
    ON CONFLICT ("instant_agit_id", "pledge_id", "slot") DO UPDATE
    SET "deco_npc_id" = EXCLUDED."deco_npc_id",
        "expire_time" = EXCLUDED."expire_time";
    RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_instantagitdeconpcreset"(
    p_instant_agit_id integer,
    p_pledge_id integer,
    p_slot integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."cached_portable_instant_agit_deco_npc" AS npc
    WHERE npc."instant_agit_id" = p_instant_agit_id
      AND npc."pledge_id" = p_pledge_id
      AND npc."slot" = p_slot;
    RETURN QUERY SELECT 0;
END;
$function$;

COMMIT;
