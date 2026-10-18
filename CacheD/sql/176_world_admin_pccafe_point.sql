-- Current Cached.exe administrative AQ 113/114 account PCCafe persistence.
--
-- The current image fixes both two-integer call ABIs. Classic 3.0, modified
-- 287 and Ertheia keep identical procedure bodies over the existing
-- account_user_pccafe_point relation.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_saveaccountpccafepoint"(
    p_account_id integer,
    p_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_point < 0 THEN
        RAISE EXCEPTION
            'Not valid parameter: account id[%] point[%]',
            p_account_id,
            p_point;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."account_user_pccafe_point" AS account_point
        WHERE account_point."account_id" = p_account_id
    ) THEN
        UPDATE "world"."account_user_pccafe_point" AS account_point
        SET "point" = p_point
        WHERE account_point."account_id" = p_account_id;
    ELSE
        INSERT INTO "world"."account_user_pccafe_point" (
            "account_id",
            "point"
        ) VALUES (
            p_account_id,
            p_point
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveaccountpccafepointrelative"(
    p_account_id integer,
    p_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."account_user_pccafe_point" AS account_point
        WHERE account_point."account_id" = p_account_id
    ) THEN
        UPDATE "world"."account_user_pccafe_point" AS account_point
        SET "point" = account_point."point" + p_point
        WHERE account_point."account_id" = p_account_id;

        UPDATE "world"."account_user_pccafe_point" AS account_point
        SET "point" = 0
        WHERE account_point."account_id" = p_account_id
          AND account_point."point" <= 0;
    ELSIF p_point >= 0 THEN
        INSERT INTO "world"."account_user_pccafe_point" (
            "account_id",
            "point"
        ) VALUES (
            p_account_id,
            p_point
        );
    END IF;
END;
$function$;

COMMIT;
